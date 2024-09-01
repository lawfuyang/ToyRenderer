#pragma once

class World
{
    SingletonFunctionsSimple(World);

public:
    void Initialize();
    void Shutdown();
    void UpdateIMGUI();
    void Update();

    void CloseMap();
    void LoadMap();

    std::string m_CurrentMapFileName;
};
#define g_World World::GetInstance()
