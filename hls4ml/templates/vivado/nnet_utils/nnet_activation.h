#ifndef NNET_ACTIVATION_H_
#define NNET_ACTIVATION_H_

#include "ap_fixed.h"
#include "nnet_common.h"
#include <cmath>

namespace nnet {

struct activ_config {
    // IO size
    static const unsigned n_in = 10;

    // Internal info
    static const unsigned table_size = 1024;

    // Resource reuse info
    static const unsigned io_type = io_parallel;
    static const unsigned reuse_factor = 1;

    // Internal data type definitions
    typedef ap_fixed<18, 8> table_t;
};

// *************************************************
//       LINEAR Activation -- See Issue 53
// *************************************************
template <class data_T, class res_T, typename CONFIG_T> void linear(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        res[ii] = data[ii];
    }
}

// *************************************************
//       RELU Activation
// *************************************************
template <class data_T, class res_T, typename CONFIG_T> void relu(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg > 0)
            res[ii] = datareg;
        else
            res[ii] = 0;
    }
}

template <class data_T, class res_T, int MAX_INT, typename CONFIG_T>
void relu_max(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg < 0)
            res[ii] = 0;
        else if (datareg > MAX_INT)
            res[ii] = MAX_INT;
        else
            res[ii] = datareg;
    }
}

template <class data_T, class res_T, typename CONFIG_T> void relu6(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    relu_max<data_T, res_T, 6, CONFIG_T>(data, res);
}

template <class data_T, class res_T, typename CONFIG_T> void relu1(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    relu_max<data_T, res_T, 1, CONFIG_T>(data, res);
}

// *************************************************
//       Sigmoid Activation
// *************************************************
inline float sigmoid_fcn_float(float input) { return 1.0 / (1 + std::exp(-input)); }

template <typename CONFIG_T, int N_TABLE> void init_sigmoid_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Default logistic sigmoid function:
    //   result = 1/(1+e^(-x))
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to +8)
        float in_val = 2 * 8.0 * (ii - float(N_TABLE) / 2.0) / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = sigmoid_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void sigmoid(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t sigmoid_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t sigmoid_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_sigmoid_table<CONFIG_T, CONFIG_T::table_size>(sigmoid_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    // Index into the lookup table based on data
    int data_round;
    int index;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        data_round = data[ii] * CONFIG_T::table_size / 16;
        index = data_round + 8 * CONFIG_T::table_size / 16;
        if (index < 0)
            index = 0;
        if (index > CONFIG_T::table_size - 1)
            index = CONFIG_T::table_size - 1;
        res[ii] = (res_T)sigmoid_table[index];
    }
}

// *************************************************
//       Softmax Activation
// *************************************************

enum class softmax_implementation { latency = 0, legacy = 1, stable = 2, argmax = 3 };

inline float exp_fcn_float(float input) { return std::exp(input); }

template <class data_T, unsigned table_size> inline float softmax_real_val_from_idx(unsigned i) {
    // Treat the index as the top N bits
    static constexpr int N = ceillog2(table_size); // number of address bits for table
    data_T x(0);
    x(x.width - 1, x.width - N) = i;
    return (float)x;
}

template <class data_T, unsigned table_size> inline unsigned softmax_idx_from_real_val(data_T x) {
    // Slice the top N bits to get an index into the table
    static constexpr int N = ceillog2(table_size); // number of address bits for table
    ap_uint<N> y = x(x.width - 1, x.width - N);    // slice the top N bits of input
    return (unsigned)y(N - 1, 0);
}

template <class data_T, typename CONFIG_T>
void init_exp_table(typename CONFIG_T::exp_table_t table_out[CONFIG_T::exp_table_size], bool negative = false) {
    // The template data_T is the data type used to address the table
    for (unsigned i = 0; i < CONFIG_T::exp_table_size; i++) {
        // Slicing bits for address is going to round towards 0, so take the central value
        float x = softmax_real_val_from_idx<data_T, CONFIG_T::exp_table_size>(i) * CONFIG_T::exp_scale;
        if (negative) {
            // for normalized inputs, we keep the normalization values positive (x_bar = x_max - x)
            // so we need to negate the input (exp(-x_bar) = exp(x - x_max))
            x = -x;
        }
        typename CONFIG_T::exp_table_t exp_x = exp_fcn_float(x);
        table_out[i] = exp_x;
    }
}

template <class data_T, typename CONFIG_T>
void init_invert_table(typename CONFIG_T::inv_table_t table_out[CONFIG_T::inv_table_size]) {
    // The template data_T is the data type used to address the table
    for (unsigned i = 0; i < CONFIG_T::inv_table_size; i++) {
        float x = softmax_real_val_from_idx<data_T, CONFIG_T::inv_table_size>(i);
        typename CONFIG_T::inv_table_t inv_x = 1 / x;
        table_out[i] = inv_x;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax_latency(data_T data[CONFIG_T::n_slice], res_T res[CONFIG_T::n_slice]) {
    #pragma HLS pipeline
    // Initialize the lookup tables
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::exp_table_t exp_table[CONFIG_T::exp_table_size];
    typename CONFIG_T::inv_table_t invert_table[CONFIG_T::inv_table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::exp_table_t exp_table[CONFIG_T::exp_table_size];
    static typename CONFIG_T::inv_table_t invert_table[CONFIG_T::inv_table_size];

#endif
    if (!initialized) {
        // Note we are exponentiating the inputs, which have type data_T
        init_exp_table<data_T, CONFIG_T>(exp_table);
        // Note we are inverting the exponentials, which have type exp_table_t
        init_invert_table<typename CONFIG_T::inv_inp_t, CONFIG_T>(invert_table);
        initialized = true;
    }

    // Calculate all the e^x's
    typename CONFIG_T::accum_t exp_res[CONFIG_T::n_slice];
    #pragma HLS array_partition variable=exp_res complete
    typename CONFIG_T::inv_inp_t exp_sum(0);
    for (unsigned i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS unroll
        unsigned x = softmax_idx_from_real_val<data_T, CONFIG_T::exp_table_size>(data[i]);
        exp_res[i] = exp_table[x];
    }

    // Explicitly sum the results with an adder tree.
    // Rounding & Saturation mode, which improve accuracy, prevent Vivado from expression balancing
    Op_add<typename CONFIG_T::accum_t> op_add;
    exp_sum = reduce<typename CONFIG_T::accum_t, CONFIG_T::n_slice, Op_add<typename CONFIG_T::accum_t>>(exp_res, op_add);

    typename CONFIG_T::inv_table_t inv_exp_sum =
        invert_table[softmax_idx_from_real_val<typename CONFIG_T::inv_inp_t, CONFIG_T::inv_table_size>(exp_sum)];
    for (unsigned i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS unroll
        res[i] = exp_res[i] * inv_exp_sum;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax_stable(data_T data[CONFIG_T::n_slice], res_T res[CONFIG_T::n_slice]) {
    #pragma HLS pipeline
    // Initialize the lookup tables
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::exp_table_t exp_table[CONFIG_T::exp_table_size];
    typename CONFIG_T::inv_table_t invert_table[CONFIG_T::inv_table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::exp_table_t exp_table[CONFIG_T::exp_table_size];
    static typename CONFIG_T::inv_table_t invert_table[CONFIG_T::inv_table_size];

#endif
    if (!initialized) {
        // Note we are exponentiating the inputs, which have type data_T
        init_exp_table<typename CONFIG_T::inp_norm_t, CONFIG_T>(exp_table, true);
        // Note we are inverting the exponentials, which have type exp_table_t
        init_invert_table<typename CONFIG_T::inv_inp_t, CONFIG_T>(invert_table);
        initialized = true;
    }

    // Find the max and compute all delta(x_i, x_max)
    Op_max<data_T> op_max;
    data_T x_max = reduce<data_T, CONFIG_T::n_slice, Op_max<data_T>>(data, op_max);

    typename CONFIG_T::inp_norm_t d_xi_xmax[CONFIG_T::n_slice];
    for (unsigned i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS unroll
        d_xi_xmax[i] = x_max - data[i];
    }

    // Calculate all the e^x's
    typename CONFIG_T::accum_t exp_res[CONFIG_T::n_slice];
    #pragma HLS array_partition variable=exp_res complete
    typename CONFIG_T::inv_inp_t exp_sum(0);
    for (unsigned i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS unroll
        unsigned x = softmax_idx_from_real_val<typename CONFIG_T::inp_norm_t, CONFIG_T::exp_table_size>(d_xi_xmax[i]);
        exp_res[i] = exp_table[x];
    }

    // Explicitly sum the results with an adder tree.
    // Rounding & Saturation mode, which improve accuracy, prevent Vivado from expression balancing
    Op_add<typename CONFIG_T::accum_t> op_add;
    exp_sum = reduce<typename CONFIG_T::accum_t, CONFIG_T::n_slice, Op_add<typename CONFIG_T::accum_t>>(exp_res, op_add);

    typename CONFIG_T::inv_table_t inv_exp_sum =
        invert_table[softmax_idx_from_real_val<typename CONFIG_T::inv_inp_t, CONFIG_T::inv_table_size>(exp_sum)];
    for (unsigned i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS unroll
        res[i] = exp_res[i] * inv_exp_sum;
    }
}

template <typename CONFIG_T, int N_TABLE> void init_exp_table_legacy(typename CONFIG_T::table_t table_out[N_TABLE]) {
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to +8)
        float in_val = 2 * 8.0 * (ii - float(N_TABLE) / 2.0) / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = exp_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <typename CONFIG_T, int N_TABLE> void init_invert_table_legacy(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Inversion function:
    //   result = 1/x
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range 0 to +64)
        float in_val = 64.0 * ii / float(N_TABLE);
        // Next, compute lookup table function
        if (in_val > 0.0)
            table_out[ii] = 1.0 / in_val;
        else
            table_out[ii] = 0.0;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax_legacy(data_T data[CONFIG_T::n_slice], res_T res[CONFIG_T::n_slice]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t exp_table[CONFIG_T::exp_table_size];
    typename CONFIG_T::table_t invert_table[CONFIG_T::inv_table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t exp_table[CONFIG_T::exp_table_size];
    static typename CONFIG_T::table_t invert_table[CONFIG_T::inv_table_size];
#endif
    if (!initialized) {
        init_exp_table_legacy<CONFIG_T, CONFIG_T::exp_table_size>(exp_table);
        init_invert_table_legacy<CONFIG_T, CONFIG_T::inv_table_size>(invert_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    // Index into the lookup table based on data for exponentials
    typename CONFIG_T::table_t exp_res[CONFIG_T::n_slice]; // different, independent, fixed point precision
    typename CONFIG_T::table_t exp_diff_res;               // different, independent, fixed point precision
    data_T data_cache[CONFIG_T::n_slice];
    int data_round;
    int index;
    for (int ii = 0; ii < CONFIG_T::n_slice; ii++) {
        data_cache[ii] = data[ii];
        exp_res[ii] = 0;
    }

    for (int ii = 0; ii < CONFIG_T::n_slice; ii++) {
        for (int jj = 0; jj < CONFIG_T::n_slice; jj++) {
            if (ii == jj)
                exp_diff_res = 1;
            else {
                data_round = (data_cache[jj] - data_cache[ii]) * CONFIG_T::exp_table_size / 16;
                index = data_round + 8 * CONFIG_T::exp_table_size / 16;
                if (index < 0)
                    index = 0;
                if (index > CONFIG_T::exp_table_size - 1)
                    index = CONFIG_T::exp_table_size - 1;
                exp_diff_res = exp_table[index];
            }
            exp_res[ii] += exp_diff_res;
        }
    }

    // Second loop to invert
    for (int ii = 0; ii < CONFIG_T::n_slice; ii++) {
        int exp_res_index = exp_res[ii] * CONFIG_T::inv_table_size / 64;
        if (exp_res_index < 0)
            exp_res_index = 0;
        if (exp_res_index > CONFIG_T::inv_table_size - 1)
            exp_res_index = CONFIG_T::inv_table_size - 1;
        // typename CONFIG_T::table_t exp_res_invert = invert_table[exp_res_index];
        res[ii] = (res_T)invert_table[exp_res_index];
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax_argmax(data_T data[CONFIG_T::n_slice], res_T res[CONFIG_T::n_slice]) {
    for (int i = 0; i < CONFIG_T::n_slice; i++) {
        #pragma HLS UNROLL
        res[i] = (res_T)0;
    }

    data_T maximum = data[0];
    int idx = 0;

    for (int i = 1; i < CONFIG_T::n_slice; i++) {
        #pragma HLS PIPELINE
        if (data[i] > maximum) {
            maximum = data[i];
            idx = i;
        }
    }

    res[idx] = (res_T)1;
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax(data_T data[CONFIG_T::n_slice], res_T res[CONFIG_T::n_slice]) {
    #pragma HLS inline
    switch (CONFIG_T::implementation) {
    case softmax_implementation::latency:
        softmax_latency<data_T, res_T, CONFIG_T>(data, res);
        break;
    case softmax_implementation::stable:
        softmax_stable<data_T, res_T, CONFIG_T>(data, res);
        break;
    case softmax_implementation::legacy:
        softmax_legacy<data_T, res_T, CONFIG_T>(data, res);
        break;
    case softmax_implementation::argmax:
        softmax_argmax<data_T, res_T, CONFIG_T>(data, res);
        break;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softmax_multidim(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS inline
    #pragma HLS allocation instances = softmax<CONFIG_T> limit = CONFIG_T::parallelization_factor function
    data_T buffer_in[CONFIG_T::n_slice];
    res_T buffer_out[CONFIG_T::n_slice];
    for (signed i = 0; i < CONFIG_T::n_outer; i++) {
        #pragma HLS UNROLL
        for (signed k = 0; k < CONFIG_T::n_inner; k++) {
            #pragma HLS UNROLL
            for (signed j = 0; j < CONFIG_T::n_slice; j++) {
                #pragma HLS UNROLL
                buffer_in[j] = data[i * CONFIG_T::n_slice * CONFIG_T::n_inner + j * CONFIG_T::n_inner + k];
            }
            softmax<data_T, res_T, CONFIG_T>(buffer_in, buffer_out);
            for (signed j = 0; j < CONFIG_T::n_slice; j++) {
                #pragma HLS UNROLL
                res[i * CONFIG_T::n_slice * CONFIG_T::n_inner + j * CONFIG_T::n_inner + k] = buffer_out[j];
            }
        }
    }
}

// *************************************************
//       TanH Activation
// *************************************************
template <typename CONFIG_T, int N_TABLE> void init_tanh_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Implement tanh lookup
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -4 to +4)
        float in_val = 2 * 4.0 * (ii - float(N_TABLE) / 2.0) / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = tanh(in_val);
        // std::cout << "Tanh:  Lookup table Index: " <<  ii<< " In Value: " << in_val << " Result: " << real_val <<
        // std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class res_T, typename CONFIG_T> void tanh(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t tanh_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t tanh_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_tanh_table<CONFIG_T, CONFIG_T::table_size>(tanh_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    // Index into the lookup table based on data
    int data_round;
    int index;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        data_round = data[ii] * CONFIG_T::table_size / 8;
        index = data_round + 4 * CONFIG_T::table_size / 8;
        // std::cout << "Input: "  << data[ii] << " Round: " << data_round << " Index: " << index << std::endl;
        if (index < 0)
            index = 0;
        if (index > CONFIG_T::table_size - 1)
            index = CONFIG_T::table_size - 1;
        res[ii] = (res_T)tanh_table[index];
    }
}

// *************************************************
//       UnaryLUT Activation
// *************************************************
template <int table_size, class data_T> inline unsigned get_index_unary_lut(data_T x) {
    // Slice the top N bits to get an index into the table
    static constexpr int N = ceillog2(table_size);
    return (unsigned)(x(x.width - 1, 0));
}

template <class data_T, class res_T, typename CONFIG_T>
void unary_lut(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in],
               typename CONFIG_T::table_t table[CONFIG_T::table_size]) {
    #pragma HLS function_instantiate variable=table
    #pragma HLS ARRAY_PARTITION variable=table

    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        #pragma HLS UNROLL
        unsigned index = get_index_unary_lut<CONFIG_T::table_size>(data[ii]);
        res[ii] = (res_T)table[index];
    }
}

// *************************************************
//       Hard sigmoid Activation
// *************************************************
template <class data_T, class res_T, typename CONFIG_T>
void hard_sigmoid(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        auto datareg = CONFIG_T::slope * data[ii] + CONFIG_T::shift;
        if (datareg > 1)
            datareg = 1;
        else if (datareg < 0)
            datareg = 0;
        res[ii] = datareg;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void hard_tanh(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    if (CONFIG_T::io_type == io_parallel) {
        #pragma HLS PIPELINE
    }

    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        auto sigmoid = CONFIG_T::slope * data[ii] + CONFIG_T::shift;
        if (sigmoid > 1)
            sigmoid = 1;
        else if (sigmoid < 0)
            sigmoid = 0;
        res[ii] = 2 * sigmoid - 1;
    }
}

// *************************************************
//       Leaky RELU Activation
// *************************************************
template <class data_T, class param_T, class res_T, typename CONFIG_T>
void leaky_relu(data_T data[CONFIG_T::n_in], param_T alpha, res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg > 0)
            res[ii] = datareg;
        else
            res[ii] = alpha * datareg;
    }
}

// *************************************************
//       Thresholded RELU Activation
// *************************************************
template <class data_T, class param_T, class res_T, typename CONFIG_T>
void thresholded_relu(data_T data[CONFIG_T::n_in], param_T theta, res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg > theta)
            res[ii] = datareg;
        else
            res[ii] = 0;
    }
}

// *************************************************
//       Softplus Activation
// *************************************************
inline float softplus_fcn_float(float input) { return std::log(std::exp(input) + 1.); }

template <typename CONFIG_T, int N_TABLE> void init_softplus_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Default softplus function:
    //   result = log(exp(x) + 1)
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to +8)
        float in_val = 2 * 8.0 * (ii - float(N_TABLE) / 2.0) / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = softplus_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softplus(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t softplus_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t softplus_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_softplus_table<CONFIG_T, CONFIG_T::table_size>(softplus_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    // Index into the lookup table based on data
    int data_round;
    int index;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        data_round = data[ii] * CONFIG_T::table_size / 16;
        index = data_round + 8 * CONFIG_T::table_size / 16;
        if (index < 0)
            index = 0;
        if (index > CONFIG_T::table_size - 1)
            index = CONFIG_T::table_size - 1;
        res[ii] = (res_T)softplus_table[index];
    }
}

// *************************************************
//       Softsign Activation
// *************************************************
inline float softsign_fcn_float(float input) { return input / (std::abs(input) + 1.); }

template <typename CONFIG_T, int N_TABLE> void init_softsign_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Default softsign function:
    //   result = x / (abs(x) + 1)
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to +8)
        float in_val = 2 * 8.0 * (ii - float(N_TABLE) / 2.0) / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = softsign_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class res_T, typename CONFIG_T>
void softsign(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t softsign_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t softsign_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_softsign_table<CONFIG_T, CONFIG_T::table_size>(softsign_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    // Index into the lookup table based on data
    int data_round;
    int index;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        data_round = data[ii] * CONFIG_T::table_size / 16;
        index = data_round + 8 * CONFIG_T::table_size / 16;
        if (index < 0)
            index = 0;
        if (index > CONFIG_T::table_size - 1)
            index = CONFIG_T::table_size - 1;
        res[ii] = (res_T)softsign_table[index];
    }
}

// *************************************************
//       ELU Activation
// *************************************************
inline float elu_fcn_float(float input) { return std::exp(input) - 1.; }

template <typename CONFIG_T, int N_TABLE> void init_elu_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Default ELU function:
    //   result = alpha * (e^(x) - 1)
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to 0)
        float in_val = -8.0 * ii / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = elu_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class param_T, class res_T, typename CONFIG_T>
void elu(data_T data[CONFIG_T::n_in], const param_T alpha, res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t elu_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t elu_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_elu_table<CONFIG_T, CONFIG_T::table_size>(elu_table);
        initialized = true;
    }

    #pragma HLS PIPELINE

    data_T datareg;
    // Index into the lookup table based on data
    int index;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg >= 0) {
            res[ii] = datareg;
        } else {
            index = datareg * CONFIG_T::table_size / -8;
            if (index > CONFIG_T::table_size - 1)
                index = CONFIG_T::table_size - 1;
            res[ii] = alpha * elu_table[index];
        }
    }
}

template <class data_T, class res_T, typename CONFIG_T> void elu(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    elu<data_T, ap_uint<1>, res_T, CONFIG_T>(data, 1.0, res);
}

// *************************************************
//       SELU Activation
// *************************************************
inline float selu_fcn_float(float input) {
    return 1.0507009873554804934193349852946 * (1.6732632423543772848170429916717 * (std::exp(input) - 1.));
}

template <typename CONFIG_T, int N_TABLE> void init_selu_table(typename CONFIG_T::table_t table_out[N_TABLE]) {
    // Default SELU function:
    //   result = 1.05 * (1.673 * (e^(x) - 1))
    for (int ii = 0; ii < N_TABLE; ii++) {
        // First, convert from table index to X-value (signed 8-bit, range -8 to 0)
        float in_val = -8.0 * ii / float(N_TABLE);
        // Next, compute lookup table function
        typename CONFIG_T::table_t real_val = selu_fcn_float(in_val);
        // std::cout << "Lookup table In Value: " << in_val << " Result: " << real_val << std::endl;
        table_out[ii] = real_val;
    }
}

template <class data_T, class res_T, typename CONFIG_T> void selu(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    // Initialize the lookup table
#ifdef __HLS_SYN__
    bool initialized = false;
    typename CONFIG_T::table_t selu_table[CONFIG_T::table_size];
#else
    static bool initialized = false;
    static typename CONFIG_T::table_t selu_table[CONFIG_T::table_size];
#endif
    if (!initialized) {
        init_selu_table<CONFIG_T, CONFIG_T::table_size>(selu_table);
        initialized = true;
    }

    typedef ap_ufixed<16, 1> selu_const_t;
    static const selu_const_t lambda = 1.0507009873554805;

    #pragma HLS PIPELINE
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        data_T datareg = data[ii];

        if (datareg >= 0) {
            // Positive branch  y = λ · x
            res[ii] = lambda * datareg;
        } else {
            // Negative branch  y = table(x)
            int index = datareg * CONFIG_T::table_size / -8;

            // clamp index to [0, table_size-1]
            if (index < 0)
                index = 0;
            else if (index > CONFIG_T::table_size - 1) {
                index = CONFIG_T::table_size - 1;
            }

            res[ii] = selu_table[index];
        }
    }
}

// *************************************************
//       PReLU Activation
// *************************************************
template <class data_T, class param_T, class res_T, typename CONFIG_T>
void prelu(data_T data[CONFIG_T::n_in], param_T alpha[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg > 0)
            res[ii] = datareg;
        else
            res[ii] = alpha[ii] * datareg;
    }
}

// *************************************************
//       Binary TanH Activation
// *************************************************
template <class data_T, class res_T, typename CONFIG_T>
void binary_tanh(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    res_T cache;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = data[ii];
        if (datareg > 0)
            cache = 1;
        else
            cache = -1;

        res[ii] = (res_T)cache;
    }
}

// *************************************************
//       Ternary TanH Activation
// *************************************************
template <class data_T, class res_T, typename CONFIG_T>
void ternary_tanh(data_T data[CONFIG_T::n_in], res_T res[CONFIG_T::n_in]) {
    #pragma HLS PIPELINE

    data_T datareg;
    res_T cache;
    for (int ii = 0; ii < CONFIG_T::n_in; ii++) {
        datareg = 2 * data[ii];
        if (datareg > 1)
            cache = 1;
        else if (datareg > -1 && datareg <= 1)
            cache = 0;
        else
            cache = -1;

        res[ii] = (res_T)cache;
    }
}

} // namespace nnet

#endif
