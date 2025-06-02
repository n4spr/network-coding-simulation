def build(bld):
    module = bld.create_ns3_module('network-coding', ['core', 'network', 'internet', 'applications'])
    
    module.source = [
        'model/galois-field.cc',
        'model/network-coding-encoder.cc',
        'model/network-coding-decoder.cc',
        'model/network-coding-tcp-application.cc',
        'helper/network-coding-helper.cc',
    ]
    
    headers = bld(features='ns3header')
    headers.module = 'network-coding'
    headers.source = [
        'model/galois-field.h',
        'model/network-coding-packet.h',
        'model/network-coding-encoder.h',
        'model/network-coding-decoder.h',
        'model/network-coding-tcp-application.h',
        'helper/network-coding-helper.h',
    ]
    
    # Example program
    if bld.env['ENABLE_EXAMPLES']:
        bld.create_ns3_program(
            'network-coding-example',
            ['network-coding', 'point-to-point'],
            source='examples/network-coding-example.cc'
        )
    
    # Tests
    if bld.env['ENABLE_TESTS']:
        module_test = bld.create_ns3_module_test_library('network-coding')
        module_test.source = ['test/network-coding-test-suite.cc']
