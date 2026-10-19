file(GLOB BMI_SRCS 
    "*.cpp"
    "BMI270_SensorAPI/*.c"
    "BMM150_SensorAPI/*.c"
)

set(BMI_INCLUDE_DIRS 
    "."
    "BMI270_SensorAPI/"
    "BMM150_SensorAPI/"
)

idf_component_register(
    SRCS ${BMI_SRCS}
    INCLUDE_DIRS ${BMI_INCLUDE_DIRS}
    REQUIRES esp_timer
    REQUIRES i2c_bus
)

