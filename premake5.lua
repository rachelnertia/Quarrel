dofile("External/Quiver/premake_quiver.lua")

workspace "Quarrel"

	SetupQuiverWorkspace()

	SetQuiverDirectory("External/Quiver/")

	Box2dProject()
	ImGuiSFMLProject()
	
	QuiverProject()

	project "Quarrel"
		kind "ConsoleApp"
		files 
		{ 
			"Source/Quarrel/**" 
		}
		includedirs
		{
			"Source/Quarrel"
		}
		IncludeQuiver()
		LinkQuiver()
