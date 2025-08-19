#include <windows.h>
#include <stdio.h>
#include <thread>

static void SpawnConsoleToggle()
{
	static bool doOnce = false;

	if (doOnce)
		return;

	doOnce = true;

	// Lambda function to check if CTRL+SHIFT+C are pressed
	auto checkKeysPressed = []() -> bool
	{
		return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && // Check if CTRL key is pressed
		       (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&   // Check if SHIFT key is pressed
		       (GetAsyncKeyState('M') & 0x8000);          // Check if C key is pressed
	};

	// Lambda function to toggle console window's visibility
	auto toggleConsoleVisibility = [&]()
	{
		AllocConsole();
		freopen("conin$", "r", stdin);
		freopen("conout$", "w", stdout);
		freopen("conout$", "w", stderr);
	};

	// Worker thread to periodically check the key combination
	std::thread worker([&]()
	{
		while (true)
		{
			if (checkKeysPressed())
			{
				toggleConsoleVisibility();
				::Sleep(5000);
			}

			::Sleep(1);
		}
	});

	worker.detach();
}
