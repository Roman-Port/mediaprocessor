#pragma once

#include <mediaprocessor.h>

int ensure_success(const char* name, int result);
int mediapro_output_wrapper(void* opaque, uint8_t* buffer, int count);