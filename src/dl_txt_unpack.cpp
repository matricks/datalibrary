/* copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info */

#include <dl/dl_txt.h>

#include "dl_types.h"
#include "dl_swap.h"
#include "dl_temp.h"
#include "container/dl_array.h"

#include <yajl/yajl_gen.h>

struct SDLUnpackContext
{
public:
	SDLUnpackContext(HDLContext _Ctx, yajl_gen _Gen) 
		: m_Ctx(_Ctx)
		, m_JsonGen(_Gen) { }

	HDLContext m_Ctx;
	yajl_gen   m_JsonGen;

 	uint AddSubDataMember(const SDLMember* _pMember, const uint8* _pData, uint32 _ArrayCount)
	{
		m_lSubdataMembers.Add(SSubDataMember(_pMember, _pData, _ArrayCount));
		return uint(m_lSubdataMembers.Len() - 1);
	}

	uint FindSubDataMember(const uint8* _pData)
	{
		for (uint iSubdata = 0; iSubdata < m_lSubdataMembers.Len(); ++iSubdata)
			if (m_lSubdataMembers[iSubdata].m_pData == _pData)
				return iSubdata;

		return uint(-1);
	}

	// subdata!
	struct SSubDataMember
	{
		SSubDataMember() {}
		SSubDataMember(const SDLMember* _pMember, const uint8* _pData, uint32 _ArrayCount)
			: m_pMember(_pMember)
			, m_pData(_pData)
			, m_ArrayCount(_ArrayCount) {}
		const SDLMember* m_pMember;
		const uint8*     m_pData;
		uint32           m_ArrayCount;
	};

	CArrayStatic<SSubDataMember, 128> m_lSubdataMembers;
};

static void DLWritePodMember(yajl_gen _Gen, EDLType _PodType, const uint8* _pData)
{
	switch(_PodType & DL_TYPE_STORAGE_MASK)
	{
		case DL_TYPE_STORAGE_FP32: yajl_gen_double( _Gen, *(fp32*)_pData); return;
		case DL_TYPE_STORAGE_FP64: yajl_gen_double( _Gen, *(fp64*)_pData); return;
		default: /*ignore*/ break;
	}

	const char* Fmt = "";
	union { int64  m_Signed; uint64 m_Unsigned; } Val;

	switch(_PodType & DL_TYPE_STORAGE_MASK)
	{
		case DL_TYPE_STORAGE_INT8:   Fmt = "%lld"; Val.m_Signed = int64(*(int8*)_pData); break;
		case DL_TYPE_STORAGE_INT16:  Fmt = "%lld"; Val.m_Signed = int64(*(int16*)_pData); break;
		case DL_TYPE_STORAGE_INT32:  Fmt = "%lld"; Val.m_Signed = int64(*(int32*)_pData); break;
		case DL_TYPE_STORAGE_INT64:  Fmt = "%lld"; Val.m_Signed = int64(*(int64*)_pData); break;

		case DL_TYPE_STORAGE_UINT8:  Fmt = "%llu"; Val.m_Unsigned = uint64(*(uint8*)_pData); break;
		case DL_TYPE_STORAGE_UINT16: Fmt = "%llu"; Val.m_Unsigned = uint64(*(uint16*)_pData); break;
		case DL_TYPE_STORAGE_UINT32: Fmt = "%llu"; Val.m_Unsigned = uint64(*(uint32*)_pData); break;
		case DL_TYPE_STORAGE_UINT64: Fmt = "%llu"; Val.m_Unsigned = uint64(*(uint64*)_pData); break;

		default: M_ASSERT(false && "This should not happen!"); Val.m_Unsigned = 0; break;
	}

	char Buffer64[128];
	int Chars = StrFormat(Buffer64, 128, Fmt, Val.m_Unsigned);
	yajl_gen_number(_Gen, Buffer64, Chars);
}

static void DLWriteInstance(SDLUnpackContext* _Ctx, const SDLType* _pType, const uint8* _pData, const uint8* _pDataBase)
{
	yajl_gen_map_open(_Ctx->m_JsonGen);

	for (uint32 i = 0; i < _pType->m_nMembers; ++i)
	{
		const SDLMember& Member = _pType->m_lMembers[i];

		yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)Member.m_Name, strlen(Member.m_Name));

		const uint8* pMemberData = _pData + Member.m_Offset[DL_PTR_SIZE_HOST];

		EDLType AtomType    = Member.AtomType();
		EDLType StorageType = Member.StorageType();

		switch(AtomType)
		{
			case DL_TYPE_ATOM_POD:
			{
				switch(StorageType)
				{
					case DL_TYPE_STORAGE_STRUCT:
					{
						const SDLType* pStructType = DLFindType(_Ctx->m_Ctx, Member.m_TypeID);
						if(pStructType == 0x0) { DL_LOG_DL_ERROR("Subtype for member %s not found!", Member.m_Name); return; }

						DLWriteInstance(_Ctx, pStructType, pMemberData, _pDataBase);
					}
					break;
					case DL_TYPE_STORAGE_STR:
					{
	 					pint  Offset     = *(pint*)pMemberData;
	 					char* pTheString =  (char*)(_pDataBase + Offset);
	 					yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pTheString, strlen(pTheString));
	 				}
					break;
					case DL_TYPE_STORAGE_PTR:
					{
						pint Offset = *(pint*)pMemberData;

						if(Offset == pint(-1))
							yajl_gen_null(_Ctx->m_JsonGen);
						else
						{
							uint ID = _Ctx->FindSubDataMember(_pDataBase + Offset);
							if(ID == uint(-1))
								ID = _Ctx->AddSubDataMember(&Member, _pDataBase + Offset, 0);
							yajl_gen_integer(_Ctx->m_JsonGen, long(ID));
						}
					}
					break;
					case DL_TYPE_STORAGE_ENUM:
					{
						const char* pEnumName = DLFindEnumName(_Ctx->m_Ctx, Member.m_TypeID, *(uint32*)pMemberData);
						yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pEnumName, strlen(pEnumName));
					}
					break;
					default: // default is a standard pod-type
						M_ASSERT(Member.IsSimplePod());
						DLWritePodMember(_Ctx->m_JsonGen, Member.m_Type, pMemberData);
						break;
				}
			}
			break;

			case DL_TYPE_ATOM_INLINE_ARRAY:
			{
				yajl_gen_array_open(_Ctx->m_JsonGen);

				switch(StorageType)
				{
					case DL_TYPE_STORAGE_STRUCT:
					{
						const SDLType* pSubType = DLFindType(_Ctx->m_Ctx, Member.m_TypeID);
						if(pSubType == 0x0) { DL_LOG_DL_ERROR("Type for inline-array member %s not found!", Member.m_Name); return; }

						pint Size  = pSubType->m_Size[DL_PTR_SIZE_HOST];
						pint Count = Member.m_Size[DL_PTR_SIZE_HOST] / Size;

						for(pint iElem = 0; iElem < Count; ++iElem)
							DLWriteInstance(_Ctx, pSubType, pMemberData + (iElem * Size), _pDataBase);
					}
					break;

					case DL_TYPE_STORAGE_STR:
					{
						pint Size = sizeof(char*);
						pint Count = Member.m_Size[DL_PTR_SIZE_HOST] / Size;

						for(pint iElem = 0; iElem < Count; ++iElem)
						{
							pint Offset = *(pint*)(pMemberData + (iElem * Size));
							char* pStr =   (char*)(_pDataBase + Offset);
							yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pStr, strlen(pStr));
						}
					}
					break;

					case DL_TYPE_STORAGE_ENUM:
					{
						pint Size  = sizeof(uint32);
						pint Count = Member.m_Size[DL_PTR_SIZE_HOST] / Size;

						for(pint iElem = 0; iElem < Count; ++iElem)
						{
							uint32* pEnumData = (uint32*)pMemberData;

							const char* pEnumName = DLFindEnumName(_Ctx->m_Ctx, Member.m_TypeID, pEnumData[iElem]);
							yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pEnumName, strlen(pEnumName));
						}
					}
					break;

					default: // default is a standard pod-type
					{
						M_ASSERT(Member.IsSimplePod());

						pint Size = DLPodSize(Member.m_Type); // the size of the inline-array could be saved in the aux-data only used by bit-field!
						pint Count = Member.m_Size[DL_PTR_SIZE_HOST] / Size;

						for(pint iElem = 0; iElem < Count; ++iElem)
							DLWritePodMember(_Ctx->m_JsonGen, Member.m_Type, pMemberData + (iElem * Size));
					}
					break;
				}

				yajl_gen_array_close(_Ctx->m_JsonGen);
			}
			break;

			case DL_TYPE_ATOM_ARRAY:
			{
				pint   Offset = *(pint*)  pMemberData;
				uint32 Count  = *(uint32*)(pMemberData + sizeof(void*));
				uint ID = _Ctx->AddSubDataMember(&Member, _pDataBase + Offset, Count);
				yajl_gen_integer(_Ctx->m_JsonGen, long(ID));
			}
			break;

			case DL_TYPE_ATOM_BITFIELD:
			{
				uint64 WriteMe = 0;
 				uint32 BFBits   = Member.BitFieldBits();
 				uint32 BFOffset = DLBitFieldOffset(Member.m_Size[DL_PTR_SIZE_HOST], Member.BitFieldOffset(), BFBits);
 
 				switch(Member.m_Size[DL_PTR_SIZE_HOST])
 				{
					case 1: WriteMe = DL_EXTRACT_BITS( uint64( *(uint8*)pMemberData), uint64(BFOffset), uint64(BFBits) ); break; 
 					case 2: WriteMe = DL_EXTRACT_BITS( uint64(*(uint16*)pMemberData), uint64(BFOffset), uint64(BFBits) ); break; 
 					case 4: WriteMe = DL_EXTRACT_BITS( uint64(*(uint32*)pMemberData), uint64(BFOffset), uint64(BFBits) ); break; 
 					case 8: WriteMe = DL_EXTRACT_BITS( uint64(*(uint64*)pMemberData), uint64(BFOffset), uint64(BFBits) ); break; 
 					default: 
 						M_ASSERT(false && "This should not happen!"); 
 						break;
 				}
 
				char Buffer[128];
				yajl_gen_number(_Ctx->m_JsonGen, Buffer, StrFormat(Buffer, DL_ARRAY_LENGTH(Buffer), "%llu", WriteMe));
			}
			break;

			default:
				M_ASSERT(false && "Invalid ATOM-type!");
		}
	}

	yajl_gen_map_close(_Ctx->m_JsonGen);
}

static void DLWriteRoot(SDLUnpackContext* _Ctx, const SDLType* _pType, const uint8* _pData)
{
	_Ctx->AddSubDataMember(0x0, _pData, 0); // add root-node as a subdata to be able to look it up as id 0!

	yajl_gen_map_open(_Ctx->m_JsonGen);

	yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)"root", 4);

		yajl_gen_map_open(_Ctx->m_JsonGen);

			yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)"type", 4);
			yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)_pType->m_Name, strlen(_pType->m_Name));

			yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)"data", 4);
			DLWriteInstance(_Ctx, _pType, _pData, _pData);

			if(_Ctx->m_lSubdataMembers.Len() != 1)
			{
				yajl_gen_string(_Ctx->m_JsonGen, (const unsigned char*)"subdata", 7);
				yajl_gen_map_open(_Ctx->m_JsonGen);
				
				// start at 1 to skip root-node!
				for (uint iSubData = 1; iSubData < _Ctx->m_lSubdataMembers.Len(); ++iSubData)
				{
					unsigned char Num[16];
					int Len = StrFormat((char*)Num, 16, "%u", iSubData);
					yajl_gen_string(_Ctx->m_JsonGen, Num, Len);

					const SDLMember* pMember = _Ctx->m_lSubdataMembers[iSubData].m_pMember;
					const uint8* pMemberData = _Ctx->m_lSubdataMembers[iSubData].m_pData;
					uint32 ArrayCount        = _Ctx->m_lSubdataMembers[iSubData].m_ArrayCount;

					EDLType AtomType    = pMember->AtomType();
					EDLType StorageType = pMember->StorageType();

					if (AtomType == DL_TYPE_ATOM_ARRAY) // remove if!
					{
						yajl_gen_array_open(_Ctx->m_JsonGen);

						switch(StorageType)
						{
							case DL_TYPE_STORAGE_STRUCT:
							{
								const SDLType* pSubType = DLFindType(_Ctx->m_Ctx, pMember->m_TypeID);
								if(pSubType == 0x0) { DL_LOG_DL_ERROR("Type for inline-array member %s not found!", pMember->m_Name); return; }

								pint Size = AlignUp(pSubType->m_Size[DL_PTR_SIZE_HOST], pSubType->m_Alignment[DL_PTR_SIZE_HOST]);
								for(pint iElem = 0; iElem < ArrayCount; ++iElem)
									DLWriteInstance(_Ctx, pSubType, pMemberData + (iElem * Size), _pData);
							}
							break;
							case DL_TYPE_STORAGE_STR:
							{
								for(pint iElem = 0; iElem < ArrayCount; ++iElem)
								{
									pint  Offset     = *(pint*)(pMemberData + (iElem * sizeof(char*)));
									char* pTheString =  (char*)(_pData + Offset);
									yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pTheString, strlen(pTheString));
								}
							}
							break;
							case DL_TYPE_STORAGE_ENUM:
							{
								uint32* pEnumData = (uint32*)pMemberData;

								for(pint iElem = 0; iElem < ArrayCount; ++iElem)
								{
									const char* pEnumName = DLFindEnumName(_Ctx->m_Ctx, pMember->m_TypeID, pEnumData[iElem]);
									yajl_gen_string(_Ctx->m_JsonGen, (unsigned char*)pEnumName, strlen(pEnumName));
								}
							}
							break;
							default:
							{
								pint Size = DLPodSize(pMember->m_Type);
								for(pint iElem = 0; iElem < ArrayCount; ++iElem)
									DLWritePodMember(_Ctx->m_JsonGen, pMember->m_Type, pMemberData + (iElem * Size));
							}
						}

						yajl_gen_array_close(_Ctx->m_JsonGen);
					}
					else
					{
						M_ASSERT(AtomType == DL_TYPE_ATOM_POD);
						M_ASSERT(StorageType == DL_TYPE_STORAGE_PTR);

						const SDLType* pSubType = DLFindType(_Ctx->m_Ctx, pMember->m_TypeID);
						if(pSubType == 0x0)
						{
							DL_LOG_DL_ERROR("Type for inline-array member %s not found!", pMember->m_Name);
							return; // TODO: need to report error some how!
						}

						DLWriteInstance(_Ctx, pSubType, pMemberData, _pData);
					}
				}

				yajl_gen_map_close(_Ctx->m_JsonGen);
			}

		yajl_gen_map_close(_Ctx->m_JsonGen);

	yajl_gen_map_close(_Ctx->m_JsonGen);
}

void DLCountSizeCallback(void* _pCtx, const char *_pStr, unsigned int _Len)
{
	DL_UNUSED(_pStr); *(pint*)_pCtx += pint(_Len);
}

struct SWriteTextContext
{
	char* m_pBuffer;
	pint  m_BufferSize;
	pint  m_WritePos;
};

void DLWriteTextCallback(void* _pCtx, const char *_pStr, unsigned int _Len)
{
	SWriteTextContext* pCtx = (SWriteTextContext*)_pCtx;

	if(pCtx->m_WritePos + _Len <= pCtx->m_BufferSize)
		memcpy(pCtx->m_pBuffer + pCtx->m_WritePos, _pStr, _Len);

	pCtx->m_WritePos += _Len;
}

struct SDLUnpackStorage { uint8 m_Storage[1024]; };

void* DLUnpackMallocFunc(void* _pCtx, unsigned int _Sz)
{
	SDLUnpackStorage* Storage = (SDLUnpackStorage*)_pCtx;
	M_ASSERT(_Sz <= sizeof(SDLUnpackStorage));
	return (void*)Storage->m_Storage;
}
void  DLUnpackFreeFunc(void* _pCtx, void* _pPtr)
{
	DL_UNUSED(_pCtx); DL_UNUSED(_pPtr);
	// just ignore free, the data should be allocated on the stack!
}

void* DLUnpackReallocFunc(void* _pCtx, void* _pPtr, unsigned int _Sz) 
{
	DL_UNUSED(_pCtx); DL_UNUSED(_pPtr); DL_UNUSED(_Sz);
	M_ASSERT(false && "yajl should never need to realloc");
	return 0x0;
}


static EDLError DLUnpackInternal(HDLContext _Context, const uint8* _pPackedData, pint _PackedDataSize, char* _pTxtData, pint* _pTxtDataSize)
{
	SDLDataHeader* pHeader = (SDLDataHeader*)_pPackedData;

	if(_PackedDataSize < sizeof(SDLDataHeader))   return DL_ERROR_MALFORMED_DATA;
	if( pHeader->m_Id != DL_TYPE_DATA_ID && 
		pHeader->m_Id != DL_TYPE_DATA_ID_SWAPED ) return DL_ERROR_MALFORMED_DATA;
	if( pHeader->m_Version != DL_VERSION)         return DL_ERROR_VERSION_MISMATCH;

	const SDLType* pType = DLFindType(_Context, pHeader->m_RootInstanceType);
	if(pType == 0x0)
		return DL_ERROR_TYPE_NOT_FOUND; // could not find root-type!

	static const int BEAUTIFY_OUTPUT = 1;
	yajl_gen_config Conf = { BEAUTIFY_OUTPUT, "  " };

	SWriteTextContext WriteCtx = { _pTxtData, *_pTxtDataSize, 0 };
	SDLUnpackStorage Storage;
	yajl_alloc_funcs AllocCatch = { DLUnpackMallocFunc, DLUnpackReallocFunc, DLUnpackFreeFunc, &Storage };
	
	yajl_gen Generator;

	if(_pTxtData == 0x0)
	{
		*_pTxtDataSize = 0;
		Generator = yajl_gen_alloc2(DLCountSizeCallback, &Conf, &AllocCatch, _pTxtDataSize);
	}
	else
		Generator = yajl_gen_alloc2(DLWriteTextCallback, &Conf, &AllocCatch, &WriteCtx);

	SDLUnpackContext PackCtx(_Context, Generator );

	DLWriteRoot(&PackCtx, pType, _pPackedData + sizeof(SDLDataHeader));

	yajl_gen_free(Generator);

	if(_pTxtData != 0x0 && WriteCtx.m_WritePos > WriteCtx.m_BufferSize)
		return DL_ERROR_BUFFER_TO_SMALL;

	return DL_ERROR_OK;
};

EDLError DLUnpack(HDLContext _Context, const uint8* _pPackedData, pint _PackedDataSize, char* _pTxtData, pint _TxtDataSize)
{
	return DLUnpackInternal(_Context, _pPackedData, _PackedDataSize, _pTxtData, &_TxtDataSize);
}

EDLError DLRequiredUnpackSize(HDLContext _Context, const uint8* _pPackedData, pint _PackedDataSize, pint* _pTxtDataSize)
{
	return DLUnpackInternal(_Context, _pPackedData, _PackedDataSize, 0x0, _pTxtDataSize);
}