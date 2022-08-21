/// Injector.cpp

/// Includes
// General utilities
#include <Windows.h>
// Strings so memory is managed for us.
#include <string>
// To see whether or not a file exists. There are better ways of doing it but this is the one I know of.
#include <experimental/filesystem>
// Iterating over processes.
#include <TlHelp32.h>

/// Definitions
// Size of a page https://en.wikipedia.org/wiki/Page_(computer_memory)
#define PAGE 4096u /*0x1000*/

/// Compile-time Evaluated Constants
// Image to inject, relative to the path of the injector executable.
constexpr auto FILENAME = L"Rudimentary Internal.dll";
// Flags for message boxes when something fails.
constexpr auto MESSAGEBOX_ERROR_FLAGS = MB_OK | MB_ICONERROR;
// Flags for message boxes when something succeeds.
constexpr auto MESSAGEBOX_SUCCESS_FLAGS = MB_OK | MB_ICONINFORMATION;
// Process to inject into.
constexpr auto PROCESS = L"csgo.exe";
// Flags needed for what we need to do with the process. The less permissions we give ourselves the better.
constexpr auto ACCESS_FLAGS = PROCESS_VM_WRITE // WriteProcessMemory
								| PROCESS_VM_OPERATION // VirtualAllocEx
								| PROCESS_CREATE_THREAD; // CreateRemoteThread

/// Global Variables
// Holds our file path.
std::wstring wstrFilePath;
// Handle to the target process.
HANDLE hProcess;

/// Forward Declarations
// Gets the path of the target file relative to the current executable.
bool GetFilePath( );
// Enables debug privilege so we have certain permissions to inject.
bool EnableDebugPrivilege( );
// Creates a handle to the target process.
bool CreateHandle( );
// Injects our file.
bool Inject( );

// Entry point.
int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
)
{
	// Call all of our functions to set up. If one of them fails, it will immediately go to the return and not call the next one.
	if ( !GetFilePath( )
		 || !EnableDebugPrivilege( ) 
		 || !CreateHandle( ) 
		 || !Inject( ) )
		return 1;
	return 0;
}

bool GetFilePath( )
{
	wstrFilePath.resize( MAX_PATH + 1 );
	GetModuleFileName( nullptr, &wstrFilePath[ 0 ], MAX_PATH );
	// Find the last slash in the directory to cut off the current executable's file name, so we can add the DLL's file name onto it instead.
	wstrFilePath = wstrFilePath.substr( 0, wstrFilePath.find_last_of( L'\\' ) + 1 ) + FILENAME;
	// Ensure that the file exists.
	if ( !std::experimental::filesystem::exists( wstrFilePath ) )
	{
		MessageBox( nullptr, L"The specified file to inject does not exist.", L"Error", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}
	return true;
}

bool EnableDebugPrivilege( )
{
	// Handle for
	HANDLE hToken;
	// Local Unique Identifier for debug privilege.
	LUID ldLocalDebugIdentifier;
	// Elevate our process to have the permissions we need to inject.
	if ( FALSE == OpenProcessToken( GetCurrentProcess( ), TOKEN_ADJUST_PRIVILEGES, &hToken ) // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-openprocesstoken
		 || FALSE == LookupPrivilegeValue( nullptr, SE_DEBUG_NAME, &ldLocalDebugIdentifier ) ) // https://msdn.microsoft.com/en-us/windows/desktop/aa379180
	{
		CloseHandle( hToken );
		MessageBox( nullptr, ( L"An error has occurred while acquiring permissions. Error code: " + std::to_wstring( GetLastError( ) ) ).c_str( ), L"Error", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}

	// Adjust token privileges with the local unique identifier for debug privilege with the enabled value.
	if ( FALSE == AdjustTokenPrivileges( hToken, FALSE, new TOKEN_PRIVILEGES // https://msdn.microsoft.com/en-us/library/windows/desktop/aa375202(v=vs.85).aspx
										{
											1,
											{
												 ldLocalDebugIdentifier,
												 SE_PRIVILEGE_ENABLED
											}
										}, sizeof( TOKEN_PRIVILEGES ), nullptr, nullptr ) )
	{
		CloseHandle( hToken );
		MessageBox( nullptr, ( L"An error has occurred while adjusting permissions. Error code: " + std::to_wstring( GetLastError( ) ) ).c_str( ), L"Error", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}

	CloseHandle( hToken );
	return true;
}

bool CreateHandle( )
{
	// Information about an executable.
	PROCESSENTRY32 peExecutable { sizeof peExecutable }; // https://docs.microsoft.com/en-us/windows/desktop/api/tlhelp32/ns-tlhelp32-tagprocessentry32
	// Set the last error to nothing so that if it was ERROR_NO_MORE_FILES beforehand, we wont mistakenly conclude finding the process failed.
	SetLastError( NULL );
	// Create a snapshot of all the currently running processes.
	const auto hProcesses = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, NULL ); // https://docs.microsoft.com/en-us/windows/desktop/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot

	// Could not create process list snapshot.
	if ( INVALID_HANDLE_VALUE == hProcesses )
	{
		MessageBox( nullptr, L"Unable to create process list snapshot.", L"Process Iteration Failed", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}

	// Could not retrieve first process information.
	if ( FALSE == Process32First( hProcesses, &peExecutable ) ) // https://docs.microsoft.com/en-us/windows/desktop/api/tlhelp32/nf-tlhelp32-process32first
	{
		CloseHandle( hProcesses );
		MessageBox( nullptr, L"Unable to find first process.", L"Process Iteration Failed", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}

	// Iterate over every process in the snapshot.
	do
	{
		// If the process has the executable name that we want.
		if ( !wcscmp( peExecutable.szExeFile, PROCESS ) )
			// Create a handle to the process when it is found.
			hProcess = OpenProcess( ACCESS_FLAGS, false, peExecutable.th32ProcessID ); // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-openprocess
	} while ( nullptr == hProcess && TRUE == Process32Next( hProcesses, &peExecutable ) );

	// Close our opened handle.
	CloseHandle( hProcesses );

	// If the process was not found.
	if( GetLastError( ) == ERROR_NO_MORE_FILES
		|| nullptr == hProcess )
	{
		MessageBox( nullptr, L"Failed to find or open process.", L"Process Handle Obtention Error", MESSAGEBOX_ERROR_FLAGS );
		return false;
	}

	return true;
}

bool Inject( )
{
	// Address of the file path inside of the target process.
	void* pFilePath = nullptr;
	// Handle to the thread we will create.
	HANDLE hThread = nullptr;

	// Cleans up everything and creates a messagebox to notify whether or not injection was successful.
	// Used so that there isn't a bunch of inefficient redundant statements.
	const auto fnCleanup = [ & ]( const bool bFailed, 
								  const wchar_t* wszMessage = L"Injection has failed with error code: ", 
								  const DWORD dwErrorCode = GetLastError( ) )
	{
		// If memory was allocated for the file path, free it.
		if ( nullptr != pFilePath )
		{
			VirtualFreeEx( hProcess, pFilePath, PAGE, MEM_RELEASE ); // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366894(v=vs.85).aspx
			pFilePath = nullptr;
		}

		// If a thread was created, free it.
		if ( nullptr != hThread )
		{
			CloseHandle( hThread );
			hThread = nullptr;
		}

		if ( bFailed ) // https://docs.microsoft.com/en-us/windows/desktop/debug/system-error-codes--0-499-
			MessageBox( nullptr, ( wszMessage + std::to_wstring( dwErrorCode ) ).c_str( ), L"Injection Failure", MESSAGEBOX_ERROR_FLAGS );
		else
			MessageBox( nullptr, L"Successfully injected.", L"Injection Success", MESSAGEBOX_SUCCESS_FLAGS );
		return !bFailed;
	};

	// Allocate memory for the file path inside of the target process.
	pFilePath = VirtualAllocEx( hProcess, nullptr, PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ); // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366890%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
	// If allocation failed.
	if ( nullptr == pFilePath )
		return false;

	// Write the file path into memory.
	if ( FALSE == WriteProcessMemory( hProcess, pFilePath, &wstrFilePath[ 0 ], wstrFilePath.length( ) * sizeof( wchar_t ), nullptr ) ) // https://msdn.microsoft.com/en-us/library/windows/desktop/ms681674(v=vs.85).aspx
		return fnCleanup( true );

	// Create a thread on the load library function address, and use the file path that we wrote into memory as a parameter.
	hThread = CreateRemoteThread( hProcess, nullptr, NULL, LPTHREAD_START_ROUTINE( LoadLibrary ), pFilePath, NULL, nullptr ); // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-createremotethread
	if ( nullptr == hThread )
		return fnCleanup( true );

	// Wait for the thread to terminate.
	if ( WaitForSingleObject( hThread, INFINITE ) == WAIT_FAILED ) // https://docs.microsoft.com/en-us/windows/desktop/api/synchapi/nf-synchapi-waitforsingleobject
	{
		// Use the thread exit code rather than GetLastError in messagebox.
		DWORD dwBuffer;
		GetExitCodeThread( hThread, &dwBuffer ); // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-getexitcodethread
		return fnCleanup( true, L"Injection has failed with thread exit code: ", dwBuffer );
	}
	return fnCleanup( false );
}
