// Client7z.cpp

#include "StdAfx.h"
#include <assert.h>
#include <vector>
#include <boost/thread.hpp>
#include "Common/IntToString.h"
#include "Common/MyInitGuid.h"
#include "Common/StringConvert.h"

#include "Windows/DLL.h"
#include "Windows/FileDir.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConversions.h"

#include "../../Common/FileStreams.h"

#include "../../Archive/IArchive.h"

#include "../../IPassword.h"
#include "../../MyVersion.h"
#include "../Common/LoadCodecs.h"

#include "CBuffer.h"
using namespace std;
using namespace NWindows;

HINSTANCE g_hInstance = 0;

static const char *kCopyrightString = 
	"7-Zippo -- An Archive Convertor Based on 7-Zip";

static const char *kHelpString =
"Usage: 7zippo.exe input-archive [output-archive]\n"
"Examples:\n"
"  7zippo a.zip        : convert 'a.zip' to 'a.7z'\n"
"  7zippo b.rar BB.7z  : convert 'b.rar' to 'BB.7z'\n"
"  7zippo c.docx       : convert (zipped) document 'c.docx' to 'c.7z'\n";

#define DIM(a) (sizeof(a)/sizeof(a[0]))

#ifdef _WIN32
#ifndef _UNICODE
bool g_IsNT = false;
static inline bool IsItWindowsNT()
{
	OSVERSIONINFO versionInfo;
	versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
	if (!::GetVersionEx(&versionInfo))
		return false;
	return (versionInfo.dwPlatformId == VER_PLATFORM_WIN32_NT);
}
#endif
#endif

void PrintString(const UString &s)
{
	printf("%s", (LPCSTR)GetOemString(s));
}

void PrintString(const AString &s)
{
	printf("%s", (LPCSTR)s);
}

void PrintNewLine()
{
	PrintString("\n");
}

void PrintStringLn(const AString &s)
{
	PrintString(s);
	PrintNewLine();
}

void PrintError(const AString &s)
{
	PrintNewLine();
	PrintString(s);
	PrintNewLine();
}

static HRESULT IsArchiveItemProp(IInArchive *archive, UInt32 index, PROPID propID, bool &result)
{
	NCOM::CPropVariant prop;
	RINOK(archive->GetProperty(index, propID, &prop));
	if (prop.vt == VT_BOOL)
		result = VARIANT_BOOLToBool(prop.boolVal);
	else if (prop.vt == VT_EMPTY)
		result = false;
	else
		return E_FAIL;
	return S_OK;
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
	return IsArchiveItemProp(archive, index, kpidIsDir, result);
}


static const wchar_t *kEmptyFileAlias = L"[Content]";


//////////////////////////////////////////////////////////////
// Archive Open callback class

class CArchiveOpenCallback:
	public IArchiveOpenCallback,
	public ICryptoGetTextPassword,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP1(ICryptoGetTextPassword);

	STDMETHOD(SetTotal)(const UInt64 *files, const UInt64 *bytes);
	STDMETHOD(SetCompleted)(const UInt64 *files, const UInt64 *bytes);

	STDMETHOD(CryptoGetTextPassword)(BSTR *password);

	bool PasswordIsDefined;
	UString Password;

	CArchiveOpenCallback() : PasswordIsDefined(false) {}
};

STDMETHODIMP CArchiveOpenCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
	return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
	return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::CryptoGetTextPassword(BSTR *password)
{
	if (!PasswordIsDefined)
	{
		// You can ask real password here from user
		// Password = GetPassword(OutStream);
		// PasswordIsDefined = true;
		PrintError("Password is not defined");
		return E_ABORT;
	}
	return StringToBstr(Password, password);
}


//////////////////////////////////////////////////////////////
// Archive Extracting callback class

static const wchar_t *kCantDeleteOutputFile = L"ERROR: Can not delete output file ";

static const char *kTestingString    =  "Testing     ";
static const char *kExtractingString =  "Extracting  ";
static const char *kSkippingString   =  "Skipping    ";

static const char *kUnsupportedMethod = "Unsupported Method";
static const char *kCRCFailed = "CRC Failed";
static const char *kDataError = "Data Error";
static const char *kUnknownError = "Unknown Error";

const static int prop_keys[7]={
	kpidIsDir, kpidPath, kpidSize, 
	kpidAttrib, kpidCTime, kpidATime, 
	kpidMTime,};

struct Archive
{
	vector<NWindows::NCOM::CPropVariant> props;
	CMyComPtr<IPipeBuffer> pipeStream;
	Archive()
	{
		//pipeStream = new_twice_buffer();
	}
	~Archive()
	{
	}
};

CCodecs codecs;
vector<Archive> archives;
CInFileStream* inFile;
COutFileStream *outFile;
CMyComPtr<IInStream> inStream;
CMyComPtr<ISequentialOutStream> outStream;


#define CLS_ARC_ID_ITEM(cls) ((cls).Data4[5])
#include "../Common/OpenArchive.h"
#include "../Common/ArchiveOpenCallback.h"
void create_iarchive(CMyComPtr<IInArchive>& iarchive)
{
	//static CObjectPool<IInArchive> pool;

	CArchiveOpenCallback openCallback;
	CIntVector formatIndices;
//	HRESULT result = MyOpenArchive(&codecs, formatIndices, archiveName, archiveLink, &openCallback);
	int r;
	UString s;
	openCallback.AddRef();
	HRESULT h=OpenArchive(&codecs, -1, inStream, s, &iarchive, r, s, &openCallback);
	if (h!=S_OK)
	{
		PrintError("Can not get input class object");
		getchar();
		abort();
	}
}


class CArchiveExtractCallback:
	public IArchiveExtractCallback,
	public ICryptoGetTextPassword,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP1(ICryptoGetTextPassword);

	UInt32 NumErrors;
	CArchiveExtractCallback()
	{
		NumErrors=0;
	}

	// IProgress
	STDMETHOD(SetTotal)(UInt64 size)						{return S_OK;}
	STDMETHOD(SetCompleted)(const UInt64 *completeValue)	{return S_OK;}


	CMyComPtr<IPipeBuffer> _pipeStream;
	STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode)
	{
		assert(askExtractMode==::NArchive::NExtract::NAskMode::kExtract);
		*outStream = archives[index].pipeStream;
		(*outStream)->AddRef();
		//_pipeStream = archives[index].pipeStream;
		return S_OK;
	}
	STDMETHOD(PrepareOperation)(Int32 askExtractMode)
	{
		//switch (askExtractMode)
		//{
		//case NArchive::NExtract::NAskMode::kExtract:  PrintString(kExtractingString); break;
		//case NArchive::NExtract::NAskMode::kTest:  PrintString(kTestingString); break;
		//case NArchive::NExtract::NAskMode::kSkip:  PrintString(kSkippingString); break;
		//};
		return S_OK;
	}

	STDMETHOD(SetOperationResult)(Int32 operationResult)
	{
		switch(operationResult)
		{
		case NArchive::NExtract::NOperationResult::kOK:
			break;
		default:
			{
				NumErrors++;
				PrintString("     ");
				switch(operationResult)
				{
				case NArchive::NExtract::NOperationResult::kUnSupportedMethod:
					PrintString(kUnsupportedMethod);
					break;
				case NArchive::NExtract::NOperationResult::kCRCError:
					PrintString(kCRCFailed);
					break;
				case NArchive::NExtract::NOperationResult::kDataError:
					PrintString(kDataError);
					break;
				default:
					PrintString(kUnknownError);
				}
			}
		}
		//PrintNewLine();
		//_pipeStream.Release();
		return S_OK;
	}

	STDMETHOD(CryptoGetTextPassword)(BSTR *password)
	{
		return StringToBstr(L"", password);
	}
};

void extract(UInt32 index)
{
	CArchiveExtractCallback extractCallback;
	CMyComPtr<IInArchive> iarchive;
	create_iarchive(iarchive);
	extractCallback.AddRef();

	iarchive->Extract(
		&index, 1, false, 
		&extractCallback);
}

class CArchiveUpdateCallback:
	public IArchiveUpdateCallback,
	public ICryptoGetTextPassword2,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP2(IArchiveUpdateCallback, ICryptoGetTextPassword2);

	UInt32 NumErrors;
	CArchiveUpdateCallback()
	{
		NumErrors=0;
		_count=0;
	}

	 //IProgress
	STDMETHOD(SetTotal)(UInt64 size)						{return S_OK;}
	STDMETHOD(SetCompleted)(const UInt64 *completeValue)	{return S_OK;}

	STDMETHOD(EnumProperties)(IEnumSTATPROPSTG **enumerator)
	{
		return E_NOTIMPL;
	}
	STDMETHODIMP GetUpdateItemInfo(UInt32 /* index */,
		Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
	{
		if (newData != NULL)
			*newData = BoolToInt(true);
		if (newProperties != NULL)
			*newProperties = BoolToInt(true);
		if (indexInArchive != NULL)
			*indexInArchive = (UInt32)-1;
		return S_OK;
	}
	STDMETHODIMP GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
	{
		NWindows::NCOM::CPropVariant prop;
		for (int i=0; i<DIM(prop_keys); ++i)
			if (prop_keys[i]==propID)
			{
				prop=archives[index].props[i];
				break;
			}
			prop.Detach(value);
			return S_OK;
	}

	int _count;
	CMyComPtr<IPipeBuffer> _pipeStream;
	STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream)
	{
		//printf("Index: %u\n", index);
		Archive& arc(archives[index]);
		PrintString(ConvertPropVariantToString(arc.props[1]));
		printf("\t\t%d/%d", ++_count, archives.size());
		PrintNewLine();
		arc.pipeStream = new_pipe_buffer();
		arc.pipeStream->SetSize(
			ConvertPropVariantToUInt64(arc.props[2]));
		*inStream = arc.pipeStream;
		(*inStream)->AddRef();
		//_pipeStream = arc.pipeStream;
		boost::thread start(&extract, index);
		return S_OK;
	}

	STDMETHOD(SetOperationResult)(Int32 operationResult)
	{
		switch(operationResult)
		{
		case NArchive::NExtract::NOperationResult::kOK:
			break;
		default:
			{
				NumErrors++;
				PrintString("     ");
				switch(operationResult)
				{
				case NArchive::NExtract::NOperationResult::kUnSupportedMethod:
					PrintString(kUnsupportedMethod);
					break;
				case NArchive::NExtract::NOperationResult::kCRCError:
					PrintString(kCRCFailed);
					break;
				case NArchive::NExtract::NOperationResult::kDataError:
					PrintString(kDataError);
					break;
				default:
					PrintString(kUnknownError);
				}
			}
		}
		//_pipeStream.Release();
		return S_OK;
	}

	STDMETHOD(CryptoGetTextPassword2)(Int32 *passwordIsDefined, BSTR *password)
	{
		*passwordIsDefined = BoolToInt(false);
		return StringToBstr(L"", password);
	}

};

UInt32 getProperites( CMyComPtr<IInArchive> iarchive )
{
	UInt32 NumItems;
	iarchive->GetNumberOfItems(&NumItems);
	for (UInt32 i=0; i<NumItems; ++i)
	{
		Archive task;
		task.props.resize(DIM(prop_keys));
		for (UInt32 j=0; j<DIM(prop_keys); ++j)
		{
			iarchive->GetProperty(
				i, prop_keys[j], &task.props[j]);
		}
		archives.push_back(task);
	}
	return NumItems;
}

void convert(UString& ifn, UString& ofn)
{
	PrintString("Input:  ");
	PrintString(ifn);
	PrintNewLine();
	PrintString("Output: ");
	PrintString(ofn);
	PrintNewLine();

	inStream = inFile = new CInFileStream;
	if (!inFile->Open(ifn))
	{
		PrintError("Can not open input file");
		return;
	}
	CMyComPtr<IInArchive> iarchive;
	create_iarchive(iarchive);

	outStream = outFile = new COutFileStream;
	if (!outFile->Create(ofn, true))
	{
		PrintError("can't create output archive file");
		return;
	}
	CMyComPtr<IOutArchive> oarchive;
	if (codecs.CreateOutArchive(codecs.FindFormatForExtension(L"7z"), oarchive)!=S_OK)
	{
		PrintError("Can not get output class object");
		return;
	}

	UInt32 NumItems = getProperites(iarchive);
	//boost::thread gogogo(&CArchiveCallback::extract, this, 0);
	CArchiveUpdateCallback update;		update.AddRef();
	HRESULT result = oarchive->UpdateItems(outStream, NumItems, &update);
}

void convert(char* inFile, char* outFile="")
{
	UString ifn=GetUnicodeString(inFile, CP_OEMCP);
	UString ofn=GetUnicodeString(outFile, CP_OEMCP);

	if (ofn.IsEmpty())
		for (int pos=ifn.Length()-1; pos>=0; --pos)
			if (ifn[pos]==L'/' || ifn[pos]==L'\\')
			{
				ofn = ifn + L".7z";
				break;
			}
			else if (ifn[pos]==L'.')
			{
				ofn = ifn;
				ofn.Delete(pos, 10000);
				ofn += L".7z";
				break;
			}

	convert(ifn, ofn);	
}

//////////////////////////////////////////////////////////////////////////
// Main function
#include <windows.h>
#include <MMSystem.h>
#pragma comment(lib, "winmm.lib")
class Timer
{
public:
	Timer()
	{
		t0=::timeGetTime();
	}

	~Timer()
	{
		DWORD t=::timeGetTime()-t0;
		ShowTime(t);
		printf("\n");
	}

	static void ShowTime( DWORD dt )
	{
		if (dt<1000)			printf("Time Consumed: %u ms\n", dt);
		else if (dt<60*1000)	printf("Time Consumed: %0.3f s\n", double(dt)/1000);
		else if (dt<3600*1000)	printf("Time Consumed: %u:%0.3f\n", dt/60000, double(dt%60000)/1000);
		else					printf("Time Consumed: %u ms\n", dt);
	}

protected:
	DWORD t0;
};

int MY_CDECL main(int argc, char* argv[])
{
#ifdef _WIN32
#ifndef _UNICODE
	g_IsNT = IsItWindowsNT();
#endif
#endif

	PrintStringLn(kCopyrightString);

	if (argc<2 || argc >3)
	{
		PrintStringLn(kHelpString);
		return 1; 
	}

	if (codecs.Load()!=S_OK)
	{
		PrintError("Can not load library");
		return 1;
	}

	Timer timer;
	codecs.AddRef();
	assert(argc==2 || argc==3);
	if (argc==2) convert(argv[1]);
	else convert(argv[1], argv[2]);
	return 0;
}
