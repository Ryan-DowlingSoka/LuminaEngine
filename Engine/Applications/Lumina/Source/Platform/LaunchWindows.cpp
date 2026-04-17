
#include <Windows.h>

extern int LuminaMain(int ArgC, char** ArgV);

int WINAPI WinMain(_In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ char* pCmdLine, _In_ int nCmdShow)
{
    return LuminaMain(__argc, __argv);
}