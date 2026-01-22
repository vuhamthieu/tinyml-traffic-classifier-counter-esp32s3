file(GLOB_RECURSE SOURCES 
    "edge-impulse-sdk/*.cpp" 
    "edge-impulse-sdk/*.c" 
    "edge-impulse-sdk/*.cc"
    "edge-impulse-sdk/*.s" 
    "tflite-model/*.cpp" 
    "tflite-model/*.c" 
    "tflite-model/*.cc"
    "model-parameters/*.cpp"
    "model-parameters/*.c"
)

idf_component_register(
    SRCS ${SOURCES} 
    INCLUDE_DIRS
        "."
        "edge-impulse-sdk"
        "edge-impulse-sdk/classifier"    
        "edge-impulse-sdk/porting"       
        "edge-impulse-sdk/dsp"           
        "edge-impulse-sdk/third_party"   
        "model-parameters"
        "tflite-model"
    REQUIRES
        json
        esp_timer
        esp-dsp
        driver
        freertos
        log
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -w 
    -fno-rtti
)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>
)