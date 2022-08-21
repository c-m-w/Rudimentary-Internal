/// Rudimentary Internal.cpp

#include <Windows.h>

/// Valve Stuff, Credits @MarkHC
#include "Valve/Valve.hpp"

#define LIFE_ALIVE	( 0 )
#define MAX_PLAYERS ( 64 )

/// Namespaces for Organization
// Contains information about modules.
namespace Modules
{
	// Module handles.
	HMODULE hClient;
	HMODULE hEngine;
	HMODULE hVGUI;
	HMODULE hVGUI2;

	// Module factories.
	CreateInterfaceFn fnciClient;
	CreateInterfaceFn fnciEngine;
	CreateInterfaceFn fnciVGUI;
	CreateInterfaceFn fnciVGUI2;
}

// Contains interface pointers.
namespace Interfaces
{
	// Interface information can be found here: https://github.com/alliedmodders/hl2sdk
	// Getting entity pointers.
	IClientEntityList* pEntityList = nullptr;
	// Drawing functions.
	ISurface* pSurface = nullptr;
	// Hooking PaintTraverse to draw.
	IPanel* pPanel = nullptr;
	// General utilities such as getting whether or not the player is in game, window size, etc.
	IVEngineClient* pEngineClient = nullptr;
}

// Contains information for hooking functions in the interfaces.
namespace Hooks
{
	// Function Types
	typedef void( __thiscall* paint_traverse_t )( IPanel*, vgui::VPANEL, bool, bool );
	typedef bool( __thiscall* is_player_t )( void* );

	// Virtual Table Addresses
	uintptr_t* pNewPanelTable, *pOldPanelTable;

	// Hooked Functions
	void __stdcall PaintTraverse( vgui::VPANEL panel, bool forceRepaint, bool allowForce );
}

// Contains indicies for virtual functions.
namespace TableIndicies
{
	constexpr auto PAINT_TRAVERSE = 41u;
	constexpr auto IS_PLAYER = 153;
}

// Contains offsets to get player information.
namespace Offsets
{
	constexpr auto TEAM = 0xF4;
	constexpr auto LIFE_STATE = 0x25F;
	constexpr auto ORIGIN = 0x138;
}

// Contains versions of interfaces
namespace Versions
{
	constexpr auto ENTITY_LIST = "VClientEntityList003";
	constexpr auto ENGINE_CLIENT = "VEngineClient014";
	constexpr auto SURFACE = "VGUI_Surface031";
	constexpr auto PANEL = "VGUI_Panel009";
}

/// Function Forward Declarations
// Sets up the cheat, then waits for the library to be freed.
DWORD WINAPI Setup( LPVOID pImageBase );
// Gets handles to required modules
bool FindModules( );
// Gets requied interfaces.
bool FindInterfaces( );
// Sets the required hook(s).
bool SetHooks( );
// Unsets hooks to free the library successfully without crashing.
void UnsetHooks( );

// Entry point.
BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
)
{
	if( fdwReason == DLL_PROCESS_ATTACH )
	{
		// Disable DLL_THREAD_ATTACH and DLL_THREAD_DETACH calls to DllMain.
		DisableThreadLibraryCalls( hinstDLL ); // https://msdn.microsoft.com/en-us/library/windows/desktop/ms682579(v=vs.85).aspx
		// Create a thread to set up the cheat.
		CreateThread( nullptr, NULL, Setup, hinstDLL, NULL, nullptr ); // https://docs.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-createthread
	}
	return TRUE;
}

using namespace Modules;
using namespace Interfaces;
using namespace Hooks;

DWORD WINAPI Setup( LPVOID pImageBase )
{
	// Call initialization functions
	if ( !FindModules( )
		 || !FindInterfaces( )
		 || !SetHooks( ) )
		return FALSE;

	// Wait to free the library and exit the current thread.
	while ( !GetAsyncKeyState( VK_HOME ) )
		Sleep( 100 );

	// Reset hooks so that when the library is freed, the game doesn't crash when attempting to access memory that was freed with the library.
	UnsetHooks( );
	// Frees the current library from the process and safely exits the threads.
	FreeLibraryAndExitThread( HMODULE( pImageBase ), TRUE ); // https://msdn.microsoft.com/en-us/library/windows/desktop/ms683153(v=vs.85).aspx
	// Cannot return any value here because the thread will be terminated in the line above.
}

bool FindModules( )
{
	return nullptr != ( hClient = GetModuleHandle( L"client_panorama.dll" ) )
		&& nullptr != ( hEngine = GetModuleHandle( L"engine.dll" ) )
		&& nullptr != ( hVGUI = GetModuleHandle( L"vguimatsurface.dll" ) )
		&& nullptr != ( hVGUI2 = GetModuleHandle( L"vgui2.dll" ) );
}

bool FindInterfaces( )
{
	// Get module factories to create pointers to interfaces.
	fnciClient = reinterpret_cast< CreateInterfaceFn >( GetProcAddress( hClient, "CreateInterface" ) );
	fnciEngine = reinterpret_cast< CreateInterfaceFn >( GetProcAddress( hEngine, "CreateInterface" ) );
	fnciVGUI = reinterpret_cast< CreateInterfaceFn >( GetProcAddress( hVGUI, "CreateInterface" ) );
	fnciVGUI2 = reinterpret_cast< CreateInterfaceFn >( GetProcAddress( hVGUI2, "CreateInterface" ) );

	// Return false if getting factories failed, saving from crashes if they were nullptr.
	if ( nullptr == fnciClient
		 || nullptr == fnciEngine
		 || nullptr == fnciVGUI )
		return false;

	// Use the factories that were found to get pointers to interfaces.
	// The first parameter in the factories is the version of the interfaces, which can be found in the link containing interface information above.
	pEntityList = reinterpret_cast< IClientEntityList* >( fnciClient( Versions::ENTITY_LIST, nullptr ) );
	pEngineClient = reinterpret_cast< IVEngineClient* >( fnciEngine( Versions::ENGINE_CLIENT, nullptr ) );
	pSurface = reinterpret_cast< ISurface* >( fnciVGUI( Versions::SURFACE, nullptr ) );
	pPanel = reinterpret_cast< IPanel* >( fnciVGUI2( Versions::PANEL, nullptr ) );

	// If all of the interfaces are pointing to something, they were successfully found.
	return nullptr != pEntityList
		&& nullptr != pSurface
		&& nullptr != pPanel
		&& nullptr != pEngineClient;
}

bool SetHooks( )
{
	// Gets the estimated size of a table, in the amount of addresses; not in bytes.
	const auto fnEstimateTableLength = [ ]( uintptr_t* pTable )
	{
		auto sReturn = std::size_t( 0u );
		MEMORY_BASIC_INFORMATION mbiTable { };
		// Keep looping through the table array while the memory the table points to at that index has the correct permissions/types.
		// We use VirtualQuery to essentially get information about memory.
		while ( NULL != VirtualQuery( reinterpret_cast< LPCVOID >( pTable[ sReturn ] ), &mbiTable, sizeof mbiTable ) && // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366902%28v=vs.85%29.aspx
				mbiTable.BaseAddress != nullptr && // Ensure the base address is valid.
				mbiTable.Type != NULL && // Ensure the type is valid.
				mbiTable.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) && // Ensure the memory is still readable/writable/executable
				!( mbiTable.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) ) // Ensure that the memory doesn't have flags that prevent us from modifying/reading it.
			sReturn++;
		return sReturn;
	};

	// There is no hook class/struct for simplicity.
	// Store the table in pOldPanelTable. This can be used if someone wishes to un-hook everything to have all of the old address or call the original function, which is necessary.
	pOldPanelTable = *reinterpret_cast< uintptr_t** >( pPanel );
	// Something is wrong with the panel table.
	if ( nullptr == pOldPanelTable )
		return false;
	// Estimate the length of the table so we know how large to make the new one.
	const auto sSurfaceLength = fnEstimateTableLength( pOldPanelTable );
	// Something is wrong with the panel table.
	if ( sSurfaceLength == NULL )
		return false;
	// Create the new table.
	pNewPanelTable = new uintptr_t[ sSurfaceLength ];
	// Copy all the old table's contents into the new one, so the functions can be called as they would normally.
	memcpy( pNewPanelTable, pOldPanelTable, sSurfaceLength * sizeof( uintptr_t ) );
	// Set the array of addresses that the table points to to the new table.
	*reinterpret_cast< uintptr_t** >( pPanel ) = pNewPanelTable;
	// Modify the address at the paint traverse index to the address of our function, so it is called instead of the original one.
	pNewPanelTable[ TableIndicies::PAINT_TRAVERSE ] = uintptr_t( PaintTraverse );

	return true;
}

void UnsetHooks( )
{
	// Set the address of the array of addresses back to the old table.
	*reinterpret_cast< uintptr_t** >( pPanel ) = pOldPanelTable;
	// Free the memory we allocated for the new table.
	delete pNewPanelTable;
}

// Converts a three-dimensional world coordinate to a two-dimensional screen coordinate.
// Returns false if the coordinate is off the screen/window.
bool WorldToScreen( const Vector vecEntityPosition, Vector &vecScreenPosition )
{
	int iWidth, iHeight;
	pEngineClient->GetScreenSize( iWidth, iHeight );
	auto vmMatrix = pEngineClient->WorldToScreenMatrix( );

	const auto flTemp = vmMatrix[ 3 ][ 0 ] * vecEntityPosition.x + vmMatrix[ 3 ][ 1 ] * vecEntityPosition.y + vmMatrix[ 3 ][ 2 ] * vecEntityPosition.z + vmMatrix[ 3 ][ 3 ];
	
	if ( flTemp <= 0.01f )
		return false;

	vecScreenPosition.x = vmMatrix[ 0 ][ 0 ] * vecEntityPosition.x + vmMatrix[ 0 ][ 1 ] * vecEntityPosition.y + vmMatrix[ 0 ][ 2 ] * vecEntityPosition.z + vmMatrix[ 0 ][ 3 ];
	vecScreenPosition.y = vmMatrix[ 1 ][ 0 ] * vecEntityPosition.x + vmMatrix[ 1 ][ 1 ] * vecEntityPosition.y + vmMatrix[ 1 ][ 2 ] * vecEntityPosition.z + vmMatrix[ 1 ][ 3 ];
	vecScreenPosition = { iWidth / 2.f + 0.5f * ( vecScreenPosition.x / flTemp ) * iWidth + 0.5f, iHeight / 2.f - 0.5f * ( vecScreenPosition.y / flTemp ) * iHeight + 0.5f, 0.f };
	return true;
}

namespace Hooks
{
	// Colors for the snaplines.
	const Color clrTeam { 0, 0, 255 }, clrEnemy { 255, 0, 0 };

	// Since we're using the __stdcall convention we don't need to worry about ecx and edx parameters. ecx is usually a pointer to the current object; the equivalent of the keyword "this"
	void __stdcall PaintTraverse( vgui::VPANEL panel, bool forceRepaint, bool allowForce )
	{
		// Find the original function address in the old table.
		static auto fnOriginal = paint_traverse_t( pOldPanelTable[ TableIndicies::PAINT_TRAVERSE ] );
		static auto vpPanelID = vgui::VPANEL( 0u );
		// Draw all of CSGO's stuff first, so we can draw on top of it.
		fnOriginal( pPanel, panel, forceRepaint, allowForce );

		// Store the correct panel id and use it to see whether or not it's the correct panel rather than doing string compares every time PaintTraverse is called for efficiency.
		if ( !vpPanelID && !strcmp( pPanel->GetName( panel ), "FocusOverlayPanel" ) )
			vpPanelID = panel;

		// Only draw to one specific panel, while in game.
		if ( vpPanelID != panel || !pEngineClient->IsInGame( ) )
			return;

		// Get the index of the local player in the entity list.
		const auto iLocalPlayerIndex = pEngineClient->GetLocalPlayer( );
		// Get the local player's team to know whether or not the entity is on our team.
		const auto iLocalPlayerTeam = *reinterpret_cast< int* >( uintptr_t( pEntityList->GetClientEntity( iLocalPlayerIndex ) ) + Offsets::TEAM );
		int iWindowWidth, iWindowHeight;
		// Get the window size to draw the snaplines directly in the center.
		pEngineClient->GetScreenSize( iWindowWidth, iWindowHeight );

		// Iterate over entities up to the index 64. Players don't have indicies over 64, which is why we can stop there since we aren't drawing to anything else.
		for( auto i = 0; i < MAX_PLAYERS; i++ )
		{
			// Don't draw a snapline to ourself.
			if ( i == iLocalPlayerIndex )
				continue;

			// Get a pointer to the entity from an index.
			auto pEntity = pEntityList->GetClientEntity( i );

			// Sanity checks, ensure the player is valid.
			if ( pEntity == nullptr // Doesn't point to a valid entity.
				 || *reinterpret_cast< int* >( uintptr_t( pEntity ) + Offsets::LIFE_STATE ) != LIFE_ALIVE // Entity isn't alive.
				 // Entities have a function at index 152 to get whether or not they are a player, such as how IPanel has PaintTraverse at index 41.
				 || !reinterpret_cast< is_player_t >( ( *reinterpret_cast< void*** >( pEntity ) )[ TableIndicies::IS_PLAYER ] )( pEntity ) ) // Entity isn't a player.
				continue;

			// Get the origin of the player.
			const auto vecOrigin = *reinterpret_cast< Vector* >( uintptr_t( pEntity ) + Offsets::ORIGIN );
			// Create a vector to store the screen coordinates.
			Vector vecScreenPosition;

			// Convert the player's world coordinates to a screen coordinate.
			if ( !WorldToScreen( vecOrigin, vecScreenPosition ) )
				continue;

			// See if the enemy is on our team or not to determine which color the line should be drawn with.
			const auto bTeammate = iLocalPlayerTeam == *reinterpret_cast< int* >( uintptr_t( pEntity ) + Offsets::TEAM );
			// Set the color of the line to the team or enemy color
			pSurface->DrawSetColor( bTeammate ? clrTeam : clrEnemy );
			// Draw a line from the center of the screen to the origin of the enemy.
			pSurface->DrawLine( iWindowWidth / 2, iWindowHeight, vecScreenPosition[ 0 ], vecScreenPosition[ 1 ] );
		}
	}
}
