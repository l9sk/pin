
/*
	This simple pin tool allows you to trace/log Heap related operations
	written by corelanc0d3r
	www.corelan.be

	Copyright (c) 2015, Corelan GCV
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.

	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.

	* Neither the name of pin nor the names of its
	  contributors may be used to endorse or promote products derived from
	  this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "pin.H"
namespace WINDOWS
{
#include<Windows.h>
}
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <ctime>

/* ================================================================== */
// Global variables 
/* ================================================================== */


BOOL LogAlloc = true;
BOOL LogFree = true;
BOOL ShowTimeStamp = false;
BOOL SplitFiles = false;
BOOL StaySilent = false;
TLS_KEY alloc_key;
FILE* LogFile;
FILE* ExceptionLogFile;
std::map<ADDRINT,WINDOWS::DWORD> chunksizes;

/* ================================================================== */
// Classes 
/* ================================================================== */

class ModuleImage
{
public:
	string ImageName;
	ADDRINT ImageBase;
	ADDRINT ImageEnd;

	void save_to_log()
	{
		std::fprintf(LogFile, "** Module %s loaded at 0x%p**\n", ImageName, ImageBase);
	}
};

class HeapOperation
{
public:
	string operation_type;
	ADDRINT chunk_start;
	WINDOWS::DWORD chunk_size;
	ADDRINT chunk_end;
	ADDRINT saved_return_pointer;
	string srp_imagename;
	time_t operation_timestamp;

	void save_to_log()
	{
		int currentpid = PIN_GetPid();
		char * ascii_time;
		if (ShowTimeStamp)
		{
			struct tm * timeinfo;
			time ( & operation_timestamp );
			timeinfo = localtime ( & operation_timestamp);
			ascii_time = strtok(asctime(timeinfo),"\n");
		}
		else
		{
			ascii_time = "";
		}

		if (!StaySilent)
		{
			if (operation_type ==  "rtlallocateheap")
			{
				std::fprintf(LogFile, "PID: %u | %s | alloc(0x%p) at 0x%p from 0x%p (%s)\n",currentpid,ascii_time,chunk_size,chunk_start,saved_return_pointer,srp_imagename);
			}
			else if (operation_type ==  "rtlreallocateheap")
			{
				std::fprintf(LogFile, "PID: %u | %s | realloc(0x%p) at 0x%p from 0x%p (%s)\n",currentpid,ascii_time,chunk_size,chunk_start,saved_return_pointer,srp_imagename);
			}
			else if (operation_type ==  "virtualalloc")
			{
				std::fprintf(LogFile, "PID: %u | %s | virtualalloc(0x%p) at 0x%p from 0x%p (%s)\n",currentpid,ascii_time,chunk_size,chunk_start,saved_return_pointer,srp_imagename);
			}
			else if (operation_type == "rtlfreeheap")
			{
				std::fprintf(LogFile, "PID: %u | %s | free(0x%p) from 0x%p (size was 0x%p) (%s)\n",currentpid,ascii_time, chunk_start,saved_return_pointer,chunk_size,srp_imagename);
			}
		}
	}

};


// more globals

vector<HeapOperation> arrAllOperations;
vector<ModuleImage> arrLoadedModules;



/* ===================================================================== */
// Command line switches
/* ===================================================================== */

KNOB<BOOL>   KnobLogAlloc(KNOB_MODE_WRITEONCE,  "pintool",
	"logalloc", "1", "Log heap allocations (RtlAllocateHeap, RtlReAllocateHeap and VirtualAlloc)");

KNOB<BOOL>   KnobLogFree(KNOB_MODE_WRITEONCE,  "pintool",
	"logfree", "1", "Log heap free operations (RtlFreeHeap)");

KNOB<BOOL>   KnobShowTimeStamp(KNOB_MODE_WRITEONCE,  "pintool",
	"timestamp", "0", "Show timestamps in output");

KNOB<BOOL>   KnobSplitFiles(KNOB_MODE_WRITEONCE,  "pintool",
	"splitfiles", "0", "Split output into PID-specific files");

KNOB<BOOL>   KnobStaySilent(KNOB_MODE_WRITEONCE,  "pintool",
	"silent", "0", "Silent mode, do not log allocs & frees to log file");


/* ===================================================================== */
// Utilities
/* ===================================================================== */

INT32 Usage()
{

	return -1;
}

void CloseLogFile()
{
	std::fprintf(ExceptionLogFile,"\nClosing log file for PID %u\n", PIN_GetPid());
	std::fprintf(LogFile, "############## EOF\n");
	fflush(LogFile);
	fclose(LogFile);
}

void CloseExceptionLogFile()
{
	std::fprintf(ExceptionLogFile,"Closing exception log file for PID %u\n", PIN_GetPid());
	std::fprintf(ExceptionLogFile, "############## EOF\n");
	fflush(ExceptionLogFile);
	fclose(ExceptionLogFile);
}


string getCurrentDateTimeStr()
{
	time_t thistime;
	thistime = time(0);
	char * ascii_time;
	struct tm * timeinfo;
	time ( & thistime);
	timeinfo = localtime ( & thistime);
	ascii_time = strtok(asctime(timeinfo),"\n");
	stringstream returnval;
	returnval << ascii_time;
	return returnval.str();
}


WINDOWS::DWORD findSize(ADDRINT address)
{
	// search in map for key address
	std::map<ADDRINT,WINDOWS::DWORD>::iterator it;

	it = chunksizes.find(address);
	if (it != chunksizes.end())
	{
		return it->second;
	}
	return 0;
}


ModuleImage addModuleToArray(IMG img)
{
	ModuleImage modimage;
	modimage.ImageName = IMG_Name(img);
	modimage.ImageEnd = IMG_HighAddress(img);
	modimage.ImageBase = IMG_LowAddress(img);
	arrLoadedModules.push_back(modimage);
	return modimage;
}


string getModuleImageNameByAddress(ADDRINT address)
{
	string returnval = "";
	IMG theimage;
	PIN_LockClient();
	theimage = IMG_FindByAddress(address);
	PIN_UnlockClient();
	if (IMG_Valid(theimage))
	{
		returnval = IMG_Name(theimage);
	}
	return returnval;
}


string getAddressInfo(ADDRINT address)
{
	stringstream ss;
	string info = "";
	string modulename = "";
	if (address > 0)
	{
		// check if address belongs to module or is part of heap
		modulename = getModuleImageNameByAddress(address);
		if (!modulename.empty())
		{
			ss << "(" << modulename << ")";
			info = ss.str();
		}
		else
		{
			ss << "";
			for (HeapOperation op : arrAllOperations)
			{
				if (op.chunk_start <= address && address <= op.chunk_end)
				{
					ss << op.operation_type << "(0x" << std::hex << op.chunk_size << ") ";
				}
			}
			info = ss.str();
		}
	}
	return info;
}

/* ===================================================================== */
// Analysis routines (runtime)
/* ===================================================================== */

VOID CaptureRtlAllocateHeapBefore(THREADID tid, UINT32 flags, int size)
{
	// At start of function, simply remember the requested size in TLS
	PIN_SetThreadData(alloc_key, (void *) size, tid);
}


VOID CaptureRtlAllocateHeapAfter(THREADID tid, ADDRINT addr, ADDRINT caller)
{
	// At end of function restore requested size and save data
	// avoid noise
	if (addr > 0x1000 && addr < 0x7fffffff)
	{
		//restore size (dwBytes) argument that was stored at start of function
		int size = (int) PIN_GetThreadData(alloc_key, tid);
		
		// create new object
		HeapOperation ho_alloc;
		ho_alloc.operation_type = "rtlallocateheap";
		ho_alloc.chunk_start = addr;
		ho_alloc.chunk_size = size;
		ho_alloc.chunk_end = addr + size;
		ho_alloc.saved_return_pointer = caller;
		ho_alloc.operation_timestamp = time(0);
		string imagename = getModuleImageNameByAddress(caller);
		ho_alloc.srp_imagename = imagename;

		ho_alloc.save_to_log();

		arrAllOperations.push_back(ho_alloc);
		// add to map chunksizes (or update existing entry)
		chunksizes[addr] = size;

	}
}


VOID CaptureRtlReAllocateHeapBefore(THREADID tid, UINT32 flags, int size)
{
	// At start of function, simply remember the requested size in TLS
	PIN_SetThreadData(alloc_key, (void *) size, tid);
}


VOID CaptureRtlReAllocateHeapAfter(THREADID tid, ADDRINT addr, ADDRINT caller)
{
	// At end of function restore requested size and save data
	// avoid noise
	if (addr > 0x1000 && addr < 0x7fffffff)
	{
		//restore size argument that was stored at start of function
		int size = (int) PIN_GetThreadData(alloc_key, tid);
		
		// create new object
		HeapOperation ho_alloc;
		ho_alloc.operation_type = "rtlreallocateheap";
		ho_alloc.chunk_start = addr;
		ho_alloc.chunk_size = size;
		ho_alloc.chunk_end = addr + size;
		ho_alloc.saved_return_pointer = caller;
		ho_alloc.operation_timestamp = time(0);
		string imagename = getModuleImageNameByAddress(caller);
		ho_alloc.srp_imagename = imagename;

		ho_alloc.save_to_log();

		arrAllOperations.push_back(ho_alloc);
		// add to map chunksizes
		chunksizes[addr] = size;

	}
}


VOID CaptureVirtualAllocBefore(THREADID tid, int size, int flProtect)
{
	// At start of function, simply remember the requested size in TLS
	PIN_SetThreadData(alloc_key, (void *) size, tid);
}


VOID CaptureVirtualAllocAfter(THREADID tid, ADDRINT addr, ADDRINT caller)
{
	// At end of function restore requested size and save data
	// avoid noise
		
	//restore size argument that was stored at start of function
	int size = (int) PIN_GetThreadData(alloc_key, tid);
		
	// create new object
	HeapOperation ho_alloc;
	ho_alloc.operation_type = "virtualalloc";
	ho_alloc.chunk_start = addr;
	ho_alloc.chunk_size = size;
	ho_alloc.chunk_end = addr + size;
	ho_alloc.saved_return_pointer = caller;
	ho_alloc.operation_timestamp = time(0);
	string imagename = getModuleImageNameByAddress(caller);
	ho_alloc.srp_imagename = imagename;

	ho_alloc.save_to_log();

	arrAllOperations.push_back(ho_alloc);
	// add to map chunksizes
	chunksizes[addr] = size;


}


VOID CaptureRtlFreeHeapBefore(ADDRINT addr, ADDRINT caller)
{
	
	// avoid noise
	if (addr > 0x1000 && addr < 0x7fffffff)
	{
		// create new object
		HeapOperation ho_free;
		ho_free.operation_type = "rtlfreeheap";
		ho_free.chunk_start = addr;
		ho_free.saved_return_pointer = caller;
		ho_free.operation_timestamp = time(0);

		string imagename = getModuleImageNameByAddress(caller);


		// see if we can get size from previous allocation
		WINDOWS::DWORD size = findSize(addr);		
		ho_free.chunk_size = size;
		ho_free.chunk_end = ho_free.chunk_start + size;
		ho_free.srp_imagename = imagename;

		ho_free.save_to_log();

		arrAllOperations.push_back(ho_free);

		// remove from chunksizes, because no longer relevant
		chunksizes.erase(addr);

	}
}



/* ===================================================================== */
// Instrumentation callbacks (instrumentation time)
/* ===================================================================== */

VOID AddInstrumentation(IMG img, VOID *v)
{
	// this instrumentation routine gets executed when an image is loaded

	// first, add image information to global array
	ModuleImage thisimage;
	thisimage = addModuleToArray(img);
	thisimage.save_to_log();

	// next, walk through the symbols in the symbol table to see if it contains the Heap related functions that we want to monitor
	//
	for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
	{
		string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);

		//  Find the RtlAllocateHeap() function.
		if (undFuncName == "RtlAllocateHeap" && LogAlloc)
		{
			RTN allocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
			if (RTN_Valid(allocRtn))
			{
				// Instrument to capture allocation address and original function arguments
				// at end of the RtlAllocateHeap function

				RTN_Open(allocRtn);

				std::fprintf(LogFile,"Adding instrumentation for RtlAllocateHeap (0x%p)\n", allocRtn);
                
				RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureRtlAllocateHeapBefore,
					IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END);

				// return value is the address that has been allocated
				RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR) &CaptureRtlAllocateHeapAfter,
					IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE, IARG_G_ARG0_CALLER, IARG_END);

				RTN_Close(allocRtn);
			}
		}

		//  Find the RtlReAllocateHeap() function.
		else if (undFuncName == "RtlReAllocateHeap" && LogAlloc)
		{
			RTN reallocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
			if (RTN_Valid(reallocRtn))
			{
				// Instrument to capture allocation address and original function arguments
				// at end of the RtlAllocateHeap function

				RTN_Open(reallocRtn);

				std::fprintf(LogFile,"Adding instrumentation for RtlReAllocateHeap (0x%p)\n", reallocRtn);
				// HeapHandle
				// Flags
				// MemoryPointer
				// Size
				RTN_InsertCall(reallocRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureRtlReAllocateHeapBefore,
					IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);

				// return value is the address that has been allocated
				RTN_InsertCall(reallocRtn, IPOINT_AFTER, (AFUNPTR) &CaptureRtlReAllocateHeapAfter,
					IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE, IARG_G_ARG0_CALLER, IARG_END);

				RTN_Close(reallocRtn);
			}
		}

		//  Find the VirtualAlloc() function.
		else if (undFuncName == "VirtualAlloc" && LogAlloc)
		{
			RTN vaallocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
			if (RTN_Valid(vaallocRtn))
			{
				// Instrument to capture allocation address and original function arguments
				// at end of the VirtualAlloc function

				RTN_Open(vaallocRtn);

				std::fprintf(LogFile,"Adding instrumentation for VirtualAlloc (0x%p)\n", vaallocRtn);
				// lpAddress
				// dwSize
				// flAllocationType
				// flProtect
				RTN_InsertCall(vaallocRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureVirtualAllocBefore,
					IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);

				// return value is the address that has been allocated
				RTN_InsertCall(vaallocRtn, IPOINT_AFTER, (AFUNPTR) &CaptureVirtualAllocAfter,
					IARG_THREAD_ID, IARG_FUNCRET_EXITPOINT_VALUE, IARG_G_ARG0_CALLER, IARG_END);

				RTN_Close(vaallocRtn);
			}
		}


		//  Find the RtlFreeHeap() function.
		else if (undFuncName == "RtlFreeHeap" && LogFree)
		{
			RTN freeRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
			if (RTN_Valid(freeRtn))
			{
				RTN_Open(freeRtn);

				std::fprintf(LogFile,"Adding instrumentation for RtlFreeHeap (0x%p)\n", freeRtn);
                
				RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR) &CaptureRtlFreeHeapBefore,
					IARG_FUNCARG_ENTRYPOINT_VALUE, 2,	// address
					IARG_G_ARG0_CALLER,					// saved return pointer
					IARG_END);

				RTN_Close(freeRtn);
			}
		}


	}
}


VOID LogContext(const CONTEXT *ctxt)
{
	std::fprintf(ExceptionLogFile, "Exception timestamp: %s\n", getCurrentDateTimeStr());
	std::fprintf(ExceptionLogFile, "PID %u | Exception context:\n", PIN_GetPid());
	ADDRINT EIP = PIN_GetContextReg( ctxt, REG_INST_PTR );
	ADDRINT EAX = PIN_GetContextReg( ctxt, REG_EAX );
	ADDRINT EBX = PIN_GetContextReg( ctxt, REG_EBX );
	ADDRINT ECX = PIN_GetContextReg( ctxt, REG_ECX );
	ADDRINT EDX = PIN_GetContextReg( ctxt, REG_EDX );
	ADDRINT ESP = PIN_GetContextReg( ctxt, REG_ESP );
	ADDRINT EBP = PIN_GetContextReg( ctxt, REG_EBP );
	ADDRINT ESI = PIN_GetContextReg( ctxt, REG_ESI );
	ADDRINT EDI = PIN_GetContextReg( ctxt, REG_EDI );

	string EIPinfo = getAddressInfo(EIP);
	string EAXinfo = getAddressInfo(EAX);
	string EBXinfo = getAddressInfo(EBX);
	string ECXinfo = getAddressInfo(ECX);
	string EDXinfo = getAddressInfo(EDX);
	string EBPinfo = getAddressInfo(EBP);
	string ESPinfo = getAddressInfo(ESP);
	string ESIinfo = getAddressInfo(ESI);
	string EDIinfo = getAddressInfo(EDI);

	std::fprintf(ExceptionLogFile, "EIP: 0x%p %s\n", EIP, EIPinfo);
	std::fprintf(ExceptionLogFile, "EAX: 0x%p %s\n", EAX, EAXinfo);
	std::fprintf(ExceptionLogFile, "EBX: 0x%p %s\n", EBX, EBXinfo);
	std::fprintf(ExceptionLogFile, "ECX: 0x%p %s\n", ECX, ECXinfo);
	std::fprintf(ExceptionLogFile, "EDX: 0x%p %s\n", EDX, EDXinfo);
	std::fprintf(ExceptionLogFile, "EBP: 0x%p %s\n", EBP, EBPinfo);
	std::fprintf(ExceptionLogFile, "ESP: 0x%p %s\n", ESP, ESPinfo);
	std::fprintf(ExceptionLogFile, "ESI: 0x%p %s\n", ESI, ESIinfo);
	std::fprintf(ExceptionLogFile, "EDI: 0x%p %s\n", EDI, EDIinfo);
	std::fprintf(ExceptionLogFile, "\n");
}



VOID OnException(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, const CONTEXT *ctxtFrom, CONTEXT *ctxtTo, INT32 info, VOID *v)
{
	if (reason != CONTEXT_CHANGE_REASON_EXCEPTION)
		return;

	UINT32 exceptionCode = info;
	ADDRINT	address = PIN_GetContextReg(ctxtFrom, REG_INST_PTR);
	std::fprintf(LogFile, "\n\n*** Exception at 0x%p, code 0x%x ***\n", address, exceptionCode);
	if ((exceptionCode >= 0xc0000000) && (exceptionCode <= 0xcfffffff))
	{
		std::fprintf(LogFile, "%s\n", "For more info about this exception, see exception log file ***");
		LogContext(ctxtFrom);
		CloseExceptionLogFile();
		CloseLogFile();
		PIN_ExitProcess(-1);
	}
}


BOOL FollowChild(CHILD_PROCESS childProcess, VOID * userData)
{
	std::fprintf(LogFile, "\n*******************************\nCreating child process from parent PID %u\n*******************************\n\n", PIN_GetPid());
	return true;
}



VOID Fini(INT32 code, VOID *v)
{
	std::fprintf(LogFile,"\n\nNumber of heap operations logged: %d\n",arrAllOperations.size());
	CloseLogFile();
}


/*!
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library.
	PIN_Init(argc,argv);

	// convert command line options into Global options
	LogAlloc = KnobLogAlloc.Value();
	LogFree = KnobLogFree.Value();
	ShowTimeStamp = KnobShowTimeStamp.Value(); 
	SplitFiles = KnobSplitFiles.Value();
	StaySilent = KnobStaySilent.Value();

	// define logfile name and behaviour
	int currentpid = PIN_GetPid();
	stringstream ss;
	ss << "corelan_heaplog_";
	ss << currentpid;
	ss << ".log";
	string fileName = ss.str();
	char * openMode = "w+";

	if (!SplitFiles)
	{
		fileName = "corelan_heaplog.log";
		openMode = "a+";
	}

	LogFile = fopen(fileName.c_str(),openMode);
	ExceptionLogFile = fopen("corelan_heaplog_exception.log","a+");

	std::fprintf(LogFile, "Instrumentation started\n");

	// load symbols. 
	PIN_InitSymbols();


	// we will need a way to pass data around, so we'll store stuff in TLS
	alloc_key = PIN_CreateThreadDataKey(0);

	string ascii_time;
	ascii_time = getCurrentDateTimeStr();

	std::fprintf(LogFile, "==========================================\n");
	std::fprintf(LogFile, "Date & time: %s\n", ascii_time);
	std::fprintf(LogFile,"Adding output for PID %u into this file\n", currentpid);
	if (LogAlloc)
	{
		std::fprintf(LogFile, "Logging heap alloc: YES\n");
	}
	else
	{
		std::fprintf(LogFile, "Logging heap alloc: NO\n");
	}

	if (LogFree)
	{
		std::fprintf(LogFile, "Logging heap free: YES\n");
	}
	else
	{
		std::fprintf(LogFile, "Logging heap free: NO\n");
	}


	// notify when following child process
	PIN_AddFollowChildProcessFunction(FollowChild, 0);

	// only add instrumentation if we have to :)

	if (LogAlloc || LogFree)
	{
		// Register function to be called to instrument traces
		IMG_AddInstrumentFunction(AddInstrumentation, 0);

		// Register function to be called when the application exits
		PIN_AddFiniFunction(Fini, 0);
	}

	//Handle exceptions
	PIN_AddContextChangeFunction(OnException, 0);

	std::fprintf(LogFile, "==========================================\n\n");

	// Start the program, never returns
	PIN_StartProgram();
    
	return 0;
}
