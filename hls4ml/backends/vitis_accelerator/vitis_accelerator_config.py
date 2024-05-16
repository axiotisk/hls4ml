import json
import os

class VitisAcceleratorConfig:
    def __init__(self, config):
        self.config = config.config
        accel_config = self.config.get('AcceleratorConfig', None)
        if accel_config is None:
             raise Exception('Missing AcceleratorConfig')
        
        self.board = accel_config.get('Board', 'alveo-u55c')
        self.supported_boards = json.load(open(os.path.dirname(__file__) + '/supported_boards.json'))
        if self.board in self.supported_boards.keys():
            board_info = self.supported_boards[self.board]
            self.part = board_info['part']
            if self.board.startswith('alveo'):
                self.platform = board_info['platform']
            self.memory_type = board_info['memory']['type']
            self.memory_channel_count = board_info['memory']['channels']
        else:
            raise Exception('The board does not appear in supported_boards.json file')

        if self.config.get('Part') is not None:
            if self.config.get('Part') != self.part:
                print(
                    'WARNING: You set a Part that does not correspond to the Board you specified.' 
                    'The correct Part is now set.'
                )
                self.config['Part'] = self.part
        
        self.num_kernel = accel_config.get('Num_Kernel')
        self.num_thread = accel_config.get('Num_Thread')
        self.batchsize = accel_config.get('Batchsize')        

    def get_platform(self):
        return self.platform
    
    def get_num_thread(self):
        return self.num_thread
    
    def get_num_kernel(self):
        return self.num_kernel
    
    def get_batchsize(self):
        return self.batchsize
    
    def get_memory_type(self):
        return self.memory_type
    
    def get_memory_channel_count(self):
        return self.memory_channel_count