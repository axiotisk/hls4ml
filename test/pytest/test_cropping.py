from pathlib import Path

import numpy as np
import pytest
from tensorflow.keras.layers import Cropping1D, Cropping2D
from tensorflow.keras.models import Sequential

import hls4ml

test_root_path = Path(__file__).parent

in_height = 6
in_width = 8
in_feat = 4

crop_t = 1
crop_b = 2
crop_l = 3
crop_r = 4

atol = 5e-3


@pytest.fixture(scope='module')
def data_1d():
    X = np.random.rand(100, in_width, in_feat)
    return X


@pytest.fixture(scope='module')
def data_2d():
    X = np.random.rand(100, in_height, in_width, in_feat)
    return X


@pytest.fixture(scope='module')
def keras_model_1d():
    model = Sequential()
    model.add(Cropping1D(input_shape=(in_width, in_feat), cropping=(crop_l, crop_r)))
    model.compile()
    return model


@pytest.fixture(scope='module')
def keras_model_2d():
    model = Sequential()
    model.add(Cropping2D(input_shape=(in_height, in_width, in_feat), cropping=((crop_t, crop_b), (crop_l, crop_r))))
    model.compile()
    return model


@pytest.mark.parametrize('io_type', ['io_stream', 'io_parallel'])
@pytest.mark.parametrize('backend', ['Vivado', 'Vitis'])
@pytest.mark.parametrize('model_type', ['1d', '2d'])
def test_cropping(keras_model_1d, keras_model_2d, data_1d, data_2d, model_type, io_type, backend):
    if model_type == '1d':
        model = keras_model_1d
        data = data_1d
    else:
        model = keras_model_2d
        data = data_2d

    config = hls4ml.utils.config_from_keras_model(
        model, default_precision='ap_fixed<32,1>', granularity='name', backend=backend
    )
    odir = str(test_root_path / f'hls4mlprj_cropping_{model_type}_{backend}_{io_type}')
    hls_model = hls4ml.converters.convert_from_keras_model(
        model, hls_config=config, io_type=io_type, output_dir=odir, backend=backend
    )
    hls_model.compile()

    # Predict
    y_keras = model.predict(data).flatten()
    y_hls = hls_model.predict(data).flatten()
    np.testing.assert_allclose(y_keras, y_hls, rtol=0, atol=atol, verbose=True)
