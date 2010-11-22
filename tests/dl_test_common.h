/* copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info */

#ifndef DL_DL_TEST_COMMON_H_INCLUDED
#define DL_DL_TEST_COMMON_H_INCLUDED

// header file generated from unittest-type lib
#include "generated/unittest.h"

#include <stdio.h>

#if defined(_MSC_VER)
	#define snprintf _snprintf // ugly fugly.
#endif // defined(_MSC_VER)

#define EXPECT_DL_ERR_EQ(_Expect, _Res) { EXPECT_EQ(_Expect, _Res) << "Result:   " << dl_error_to_string(_Res) ; }
#define EXPECT_DL_ERR_OK(_Res) EXPECT_DL_ERR_EQ( DL_ERROR_OK, _Res)
#define DL_ARRAY_LENGTH(Array) (sizeof(Array)/sizeof(Array[0]))

template<typename T>
const char* ArrayToString(T* _pArr, unsigned int _Count, char* pBuffer, unsigned int nBuffer)
{
	unsigned int Pos = snprintf(pBuffer, nBuffer, "{ %f", _pArr[0]);

	for(unsigned int i = 1; i < _Count && Pos < nBuffer; ++i)
	{
		Pos += snprintf(pBuffer + Pos, nBuffer - Pos, ", %f", _pArr[i]);
	}

	snprintf(pBuffer + Pos, nBuffer - Pos, " }");
	return pBuffer;
}

#define EXPECT_ARRAY_EQ(_Count, _Expect, _Actual) \
	{ \
		bool WasEq = true; \
		for(unsigned int i = 0; i < _Count && WasEq; ++i) \
			WasEq = _Expect[i] == _Actual[i]; \
		char ExpectBuf[1024]; \
		char ActualBuf[1024]; \
		char Err[2048]; \
		snprintf(Err, DL_ARRAY_LENGTH(Err), "Arrays diffed!\nExpected:\n%s\nActual:\n%s", \
											ArrayToString(_Expect, _Count, ExpectBuf, DL_ARRAY_LENGTH(ExpectBuf)), \
											ArrayToString(_Actual, _Count, ActualBuf, DL_ARRAY_LENGTH(ActualBuf))); \
		EXPECT_TRUE(WasEq) << Err; \
	}

static void* MyAlloc(unsigned int  _Size, unsigned int _Alignment) { (void)_Alignment; return malloc(_Size); }
static void  MyFree (void* _pPtr) { free(_pPtr); }

class DL : public ::testing::Test
{
protected:
	dl_alloc_functions_t MyAllocs;

	virtual void SetUp()
	{
		// bake the unittest-type library into the exe!
		static const unsigned char TypeLib[] =
		{
			#include "generated/unittest.bin.h"
		};

		MyAllocs.alloc = MyAlloc;
		MyAllocs.free  = MyFree;

		EXPECT_DL_ERR_EQ( DL_ERROR_OK, dl_context_create( &Ctx, &MyAllocs ) );
		EXPECT_DL_ERR_EQ( DL_ERROR_OK, dl_context_load_type_library(Ctx, TypeLib, sizeof(TypeLib)) );
	}

	virtual void TearDown()
	{
		EXPECT_EQ(DL_ERROR_OK, dl_context_destroy(Ctx));
	}

public:
	dl_ctx_t Ctx;
};

#endif // DL_DL_TEST_COMMON_H_INCLUDED
