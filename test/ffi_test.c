#include <stdint.h>

#ifdef _WIN32
	#define EXPORT __declspec(dllexport)
#else
	#define EXPORT
#endif

EXPORT uint64_t ffi_test_constant_42_u64()
{
	return 42;
}

EXPORT uint64_t ffi_test_add_u64(uint64_t a, uint64_t b)
{
	return a + b;
}

EXPORT uint32_t ffi_test_add_u32(uint32_t a, uint32_t b)
{
	return a + b;
}

EXPORT uint16_t ffi_test_add_u16(uint16_t a, uint16_t b)
{
	return a + b;
}

EXPORT uint8_t ffi_test_add_u8(uint8_t a, uint8_t b)
{
	return a + b;
}



EXPORT int64_t ffi_test_add_s64(int64_t a, int64_t b)
{
	return a + b;
}

EXPORT int32_t ffi_test_add_s32(int32_t a, int32_t b)
{
	return a + b;
}

EXPORT int16_t ffi_test_add_s16(int16_t a, int16_t b)
{
	return a + b;
}

EXPORT int8_t ffi_test_add_s8(int8_t a, int8_t b)
{
	return a + b;
}



EXPORT float ffi_test_add_f32(float a, float b)
{
	return a + b;
}

EXPORT float ffi_test_add_f64(double a, double b)
{
	return a + b;
}



EXPORT uint64_t ffi_test_sum_u64(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
	return a + b + c + d + e + f + g + h;
}

EXPORT uint8_t ffi_test_sum_u8(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f, uint8_t g, uint8_t h)
{
	return a + b + c + d + e + f + g + h;
}

EXPORT float ffi_test_sum_f32(float a, float b, float c, float d, float e, float f, float g, float h)
{
	return a + b + c + d + e + f + g + h;
}

EXPORT double ffi_test_sum_f64(double a, double b, double c, double d, double e, double f, double g, double h)
{
	return a + b + c + d + e + f + g + h;
}

EXPORT float ffi_test_sum_mixed_f32(float a, uint64_t x0, float b, uint64_t x1, float c, uint64_t x2, float d, uint64_t x3, float e, uint64_t x4, float f, uint64_t x5, float g, uint64_t x6, float h, uint64_t x7)
{
	return a + b + c + d + e + f + g + h;
}

EXPORT uint64_t ffi_test_sum_mixed_u64(float a, uint64_t x0, float b, uint64_t x1, float c, uint64_t x2, float d, uint64_t x3, float e, uint64_t x4, float f, uint64_t x5, float g, uint64_t x6, float h, uint64_t x7)
{
	return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7;
}
