/* copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info */

#include <dl/dl.h>
#include <dl/dl_txt.h>

#include <string.h>

// #include "dl_types.h"
#include "getopt/getopt.h"
#include "container/dl_array.h"

// #include <platform/cpu.h>
// #include <platform/string.h>
// 
// #include <common_util/getopt.h>
// 
// #include <container/array.h>

#include <stdio.h>
#include <stdlib.h>

/*
	Tool that take SBDL-data in text-form and output a packed binary.
*/

int g_Verbose = 0;
int g_Unpack = 0;

void PrintHelp(SGetOptContext* _pCtx)
{
	char Buffer[2048];
	printf("usage: dl_pack.exe [options] file_to_pack\n\n");
	printf(GetOptCreateHelpString(_pCtx, Buffer, 2048));
}

uint8* ReadFile(FILE* _pFile, pint* Size)
{
	fseek(_pFile, 0, SEEK_END);
	*Size = ftell(_pFile);
	fseek(_pFile, 0, SEEK_SET);

	uint8* pData = (uint8*)malloc(*Size + 1);
	pData[*Size] = '\0';
	fread(pData, 1, *Size, _pFile);
	return pData;
}

void* MyAlloc(pint  _Size, pint _Alignment) { (void)_Alignment; return malloc(_Size); }
void  MyFree (void* _pPtr) { free(_pPtr); }

SDLAllocFunctions g_MyAllocs = { MyAlloc, MyFree };

#define M_VERBOSE_OUTPUT(fmt, ...) if(g_Verbose) { fprintf(stderr, fmt "\n", ##__VA_ARGS__); }

#define M_ERROR_AND_FAIL(fmt, ...) { fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return 0x0; }

HDLContext CreateContext(CArrayStatic<const char*, 128>& _lLibPaths, CArrayStatic<const char*, 128>& _lLibs)
{
	HDLContext Ctx;
	EDLError err = DLContextCreate(&Ctx, &g_MyAllocs, &g_MyAllocs);
	if(err != DL_ERROR_OK)
		M_ERROR_AND_FAIL( "SBDL error while creating context: %s", DLErrorToString(err));

	// load all type-libs.
	for(pint iLib = 0; iLib < _lLibs.Len(); iLib++)
	{
		// search for lib!
		for (pint iPath = 0; iPath < _lLibPaths.Len(); ++iPath)
		{
			// build testpath.
			char Path[2048];
			uint PathLen = strlen(_lLibPaths[iPath]);
			strcpy(Path, _lLibPaths[iPath]);
			if(PathLen != 0 && Path[PathLen - 1] != '/')
				Path[PathLen++] = '/';
			strcpy(Path + PathLen, _lLibs[iLib]);

			FILE* File = fopen(Path, "rb");

			if(File != 0x0)
			{
				M_VERBOSE_OUTPUT("Reading type-library from file %s", Path);

				pint Size;
				uint8* TL = ReadFile(File, &Size);
				err = DLLoadTypeLibrary(Ctx, TL, Size);
				if(err != DL_ERROR_OK) 
					M_ERROR_AND_FAIL( "SBDL error while loading type library (%s): %s", Path, DLErrorToString(err));

				free(TL);
				fclose(File);
				break;
			}
		}
	}

	return Ctx;
}

#define M_ERROR_AND_QUIT(fmt, ...) { fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); return 1; }

int main(int argc, char** argv)
{
	static const SOption OptionList[] = 
	{
		{"help",    'h', E_OPTION_TYPE_NO_ARG,   0x0,        'h', "displays this help-message"},
		{"libpath", 'L', E_OPTION_TYPE_REQUIRED, 0x0,        'L', "add type-library include path", "path"},
		{"lib",     'l', E_OPTION_TYPE_REQUIRED, 0x0,        'l', "add type-library", "path"},
		{"output",  'o', E_OPTION_TYPE_REQUIRED, 0x0,        'o', "output to file", "file"},
		{"endian",  'e', E_OPTION_TYPE_REQUIRED, 0x0,        'e', "endianness of output data, if not specified pack-platform is assumed", "little,big"},
		{"ptrsize", 'p', E_OPTION_TYPE_REQUIRED, 0x0,        'p', "ptr-size of output data, if not specified pack-platform is assumed", "4,8"},
		{"unpack",  'u', E_OPTION_TYPE_FLAG_SET, &g_Unpack,    1, "force dl_pack to treat input data as a packed instance that should be unpacked."},
		{"verbose", 'v', E_OPTION_TYPE_FLAG_SET, &g_Verbose,   1, "verbose output"},
		{0}
	};

	SGetOptContext GOCtx;
	GetOptCreateContext(&GOCtx, argc, argv, OptionList);

	CArrayStatic<const char*, 128> lLibPaths; lLibPaths.Add("");
	CArrayStatic<const char*, 128> lLibs;
	const char* pOutput  = "";
	const char* pInput   = "";
	ECpuEndian  Endian  = ENDIAN_HOST;
	pint        PtrSize = sizeof(void*);

	int32 opt;
	while((opt = GetOpt(&GOCtx)) != -1)
	{
		switch(opt)
		{
			case 'h': PrintHelp(&GOCtx); return 0;
			case 'L': 
				if(lLibPaths.Full())
					M_ERROR_AND_QUIT("dl_pack only supports %u libpaths!", lLibPaths.Capacity());

				lLibPaths.Add(GOCtx.m_CurrentOptArg);
				break;
			case 'l':
				if(lLibs.Full())
					M_ERROR_AND_QUIT("dl_pack only supports %u type libraries libs!", lLibs.Capacity());

				lLibs.Add(GOCtx.m_CurrentOptArg);
				break;
			case 'o': 
				if(pOutput[0] != '\0')
					M_ERROR_AND_QUIT("output-file already set to: \"%s\", trying to set it to \"%s\"", pOutput, GOCtx.m_CurrentOptArg);

				pOutput = GOCtx.m_CurrentOptArg; 
				break;
			case 'e':
				if(strcmp(GOCtx.m_CurrentOptArg, "little") == 0)
					Endian = ENDIAN_LITTLE;
				else if(strcmp(GOCtx.m_CurrentOptArg, "big") == 0) 
					Endian = ENDIAN_BIG;
				else 
					M_ERROR_AND_QUIT("endian-flag need \"little\" or \"big\", not \"%s\"!", GOCtx.m_CurrentOptArg);
				break;
			case 'p':
				if(strlen(GOCtx.m_CurrentOptArg) != 1 || (GOCtx.m_CurrentOptArg[0] != '4' && GOCtx.m_CurrentOptArg[0] != '8'))
					M_ERROR_AND_QUIT("ptr-flag need \"4\" or \"8\", not \"%s\"!", GOCtx.m_CurrentOptArg);

				PtrSize = GOCtx.m_CurrentOptArg[0] - '0';
				break;
			case '!': M_ERROR_AND_QUIT("incorrect usage of flag \"%s\"!", GOCtx.m_CurrentOptArg);
			case '?': M_ERROR_AND_QUIT("unrecognized flag \"%s\"!", GOCtx.m_CurrentOptArg);
			case '+':
				if(pInput[0] != '\0')
					M_ERROR_AND_QUIT("input-file already set to: \"%s\", trying to set it to \"%s\"", pInput, GOCtx.m_CurrentOptArg);

				pInput = GOCtx.m_CurrentOptArg;
				break;
			case 0: break; // ignore, flag was set!
			default:
				M_ASSERT(false && "This should not happen!");
		}
	}

	FILE* pInFile  = stdin;
	FILE* pOutFile = stdout;

	if(pInput[0] == '\0')
		M_ERROR_AND_QUIT("Should have read input from stdin, but this is not supported yet =/");

	pInFile = fopen(pInput, "rb");
	if(pInFile == 0x0) 
		M_ERROR_AND_QUIT("Could not open input file: %s", pInput);

	if(pOutput[0] != '\0')
	{
		pOutFile = fopen(pOutput, "wb");
		if(pOutFile == 0x0) 
			M_ERROR_AND_QUIT("Could not open output file: %s", pOutput);
	}
	
	pint Size;
	uint8* InData = ReadFile(pInFile, &Size);

	HDLContext Ctx = CreateContext(lLibPaths, lLibs);
	if(Ctx == 0x0)
		return 1;

	uint8* pOutData = 0x0;
	pint   OutDataSize = 0;

	if(g_Unpack == 1) // should unpack
	{
		if(sizeof(void*) <= DLInstancePtrSize(InData, Size))
		{
			// we are converting ptr-size down and can use the faster inplace load.
			EDLError err = DLConvertInstanceInplace(Ctx, InData, Size, ENDIAN_HOST, sizeof(void*));
			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting packed instance: %s", DLErrorToString(err));
		}
		else
		{
			// instance might grow so ptr-data so the non-inplace unpack is needed.
			pint SizeAfterConvert;
			EDLError err = DLInstanceSizeConverted(Ctx, InData, Size, PtrSize, &SizeAfterConvert);

			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting endian of data: %s", DLErrorToString(err));

			uint8* pConverted = (uint8*)malloc(SizeAfterConvert);

			err = DLConvertInstance(Ctx, InData, Size, pConverted, SizeAfterConvert, ENDIAN_HOST, sizeof(void*));
			if(err != DL_ERROR_OK)
				M_ERROR_AND_QUIT( "SBDL error converting endian of data: %s", DLErrorToString(err));

			free(InData);
			InData = pConverted;
			Size   = SizeAfterConvert;
		}

		EDLError err = DLRequiredUnpackSize(Ctx, InData, Size, &OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT( "SBDL error while calculating unpack size: %s", DLErrorToString(err));

		pOutData = (uint8*)malloc(OutDataSize);

		err = DLUnpack(Ctx, InData, Size, (char*)pOutData, OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT( "SBDL error while unpacking: %s", DLErrorToString(err));
	}
	else
	{
		EDLError err = DLRequiredTextPackSize(Ctx, (char*)InData, &OutDataSize);
		if(err != DL_ERROR_OK) 
			M_ERROR_AND_QUIT("SBDL error while calculating pack size: %s", DLErrorToString(err));

		pOutData = (uint8*)malloc(OutDataSize);

		err = DLPackText(Ctx, (char*)InData, pOutData, OutDataSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT("SBDL error while packing: %s", DLErrorToString(err));

		err = DLConvertInstanceInplace(Ctx, pOutData, OutDataSize, Endian, PtrSize);
		if(err != DL_ERROR_OK)
			M_ERROR_AND_QUIT("SBDL error while converting packed instance: %s", DLErrorToString(err));
	}

	fwrite(pOutData, 1, OutDataSize, pOutFile);
	free(pOutData);
	free(InData);

	if(pInput[0] != '\0')  fclose(pInFile);
	if(pOutput[0] != '\0') fclose(pOutFile);

	return 0;
}