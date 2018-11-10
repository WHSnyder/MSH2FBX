#include "pch.h"
#include "MSH2FBX.h"
#include "CLI11.hpp"
#include "Converter.h"
#include <math.h>

namespace MSH2FBX
{
	void Log(const char* msg)
	{
		if (IsInProgress)
		{
			std::cout << "\r" << msg << std::endl;
		}
		else
		{
			std::cout << msg << std::endl;
		}
	}

	void Log(const string& msg)
	{
		Log(msg.c_str());
	}

	void LogEntry(LoggerEntry entry)
	{
		if (entry.m_Level >= ELogType::Warning)
		{
			Log(entry.m_Message.c_str());
		}
	}

	void ShowProgress(const float progress)
	{
		IsInProgress = true;

		const char ProgressBarWidth = 70;
		const float percent = ceil(progress * 100);
		const float bar = progress * ProgressBarWidth;

		std::cout << "\r" << percent << "%" << (percent / 100 >= 1 ? "" : percent / 10 >= 1 ? " " : "  ") << " [";

		for (char i = 0; i < ProgressBarWidth; ++i)
		{
			std::cout << (i < bar ? "=" : " ");
		}

		std::cout << "]";
		std::cout.flush();
	}

	void FinishProgress()
	{
		ShowProgress(1.0f);
		IsInProgress = false;
		std::cout << std::endl;
	}

	function<void(LoggerEntry)> LogCallback = LogEntry;


	bool DescribesDirectory(const fs::path Path)
	{
		fs::path filename = Path.filename();
		return filename == "" || filename == "." || filename == "..";
	}

	vector<fs::path> GetFiles(const fs::path& Directory, const string& Extension, const bool recursive)
	{
		vector<fs::path> mshFiles;
		if (!fs::exists(Directory))
		{
			return mshFiles;
		}

		for (auto& p : fs::directory_iterator(Directory))
		{
			if (p.path().extension() == Extension)
			{
				mshFiles.insert(mshFiles.end(), p.path().string());
			}

			if (recursive && p.is_directory())
			{
				vector<fs::path>& next = GetFiles(p.path(), Extension, recursive);
				mshFiles.insert(mshFiles.end(), next.begin(), next.end());
			}
		}
		return mshFiles;
	}

	vector<fs::path> GetFiles(const vector<fs::path>& Paths, const string& Extension, const bool recursive)
	{
		vector<fs::path> files;
		for (auto& it = Paths.begin(); it != Paths.end(); ++it)
		{
			if (fs::exists(*it))
			{
				if (fs::is_directory(*it))
				{
					vector<fs::path>& next = GetFiles(*it, Extension, recursive);
					files.insert(files.end(), next.begin(), next.end());
				}
				else if (fs::is_regular_file(*it))
				{
					files.insert(files.end(), *it);
				}
			}
		}
		return files;
	}

	bool ProcessMSH(fs::path mshPath, const bool overrideAnimName, const bool createFBXFile)
	{
		if (createFBXFile)
		{
			fs::path fbxPath = mshPath;
			fbxPath.replace_extension(".fbx");
			Converter::Start(fbxPath);
		}

		MSH* msh = MSH::Create();
		if (!msh->ReadFromFile(mshPath.u8string()))
		{
			MSH::Destroy(msh);
			if (createFBXFile)
			{
				Converter::Close();
			}
			return false;
		}

		if (overrideAnimName)
		{
			mshPath = mshPath.filename();
			mshPath.replace_extension("");
			Converter::OverrideAnimName = mshPath.u8string();
		}

		if (!Converter::AddMSH(msh))
		{
			MSH::Destroy(msh);
			if (createFBXFile)
			{
				Converter::Close();
			}
			return false;
		}

		Converter::OverrideAnimName = "";
		MSH::Destroy(msh);

		if (createFBXFile)
		{
			Converter::Save();
		}
		return true;
	}
}

// the main function must not lie inside a namespace
int main(int argc, char *argv[])
{
#if _DEBUG
	std::cin.get();
#endif

	using std::string;
	using std::vector;
	using std::map;
	using LibSWBF2::Logging::Logger;
	using LibSWBF2::Chunks::Mesh::MSH;
	using namespace MSH2FBX;

	Logger::SetLogCallback(MSH2FBX::LogCallback);

	vector<fs::path> files;
	vector<fs::path> animations;
	vector<fs::path> models;
	map<string, EModelPurpose> filterMap
	{
		// Meshes
		{"Mesh", EModelPurpose::Mesh},
		{"Mesh_Regular", EModelPurpose::Mesh_Regular},
		{"Mesh_Lowrez", EModelPurpose::Mesh_Lowrez},
		{"Mesh_Collision", EModelPurpose::Mesh_Collision},
		{"Mesh_VehicleCollision", EModelPurpose::Mesh_VehicleCollision},
		{"Mesh_ShadowVolume", EModelPurpose::Mesh_ShadowVolume},
		{"Mesh_TerrainCut", EModelPurpose::Mesh_TerrainCut},

		// Just Points
		{"Point", EModelPurpose::Point},
		{"Point_EmptyTransform", EModelPurpose::Point_EmptyTransform},
		{"Point_DummyRoot", EModelPurpose::Point_DummyRoot},
		{"Point_HardPoint", EModelPurpose::Point_HardPoint},

		// Skeleton
		{"Skeleton", EModelPurpose::Skeleton},
		{"Skeleton_Root", EModelPurpose::Skeleton_Root},
		{"Skeleton_BoneRoot", EModelPurpose::Skeleton_BoneRoot},
		{"Skeleton_BoneLimb", EModelPurpose::Skeleton_BoneLimb},
		{"Skeleton_BoneEnd", EModelPurpose::Skeleton_BoneEnd},
	};
	vector<string> filter;
	fs::path fbxDestination = "";

	// Build up Command Line Parser
	CLI::App app
	{
		"--------------------------------------------------------------\n"
		"-------------------- MSH to FBX Converter --------------------\n"
		"--------------------------------------------------------------\n"
		"Web: https://github.com/Ben1138/MSH2FBX \n"
		"\n"
		"This tool can convert multiple MSHs at once! If no destination\n"
		"is specified (via the -d option), all resulting FBX files will\n"
		"be placed in the exact same location where the respective MSH\n"
		"files lie.\n"
		"If some directory is specified, the resulting FBX files will end\n"
		"up in that specific directory (directory must exist!)\n"
		"If a specific FBX File Name is given, all MSHs will be merged\n"
		"into that single FBX File!\n"
		"\n"
		"Note that all resulting FBX Files will be overwritten\n"
		"without question!\n"
	};

	CLI::Option* filesOpt = app.add_option("-f,--files", files, "MSH file paths (file or directory, one or more), importing everything.")->check(CLI::ExistingPath);
	CLI::Option* animsOpt = app.add_option("-a,--animations", animations, "MSH file Paths (file or directory, one or more), importing Animation Data only!")->check(CLI::ExistingPath);
	CLI::Option* modelsOpt = app.add_option("-m,--models", models, "MSH file paths (file or directory, one or more), importing Model Data only!")->check(CLI::ExistingPath);
	CLI::Option* destOpt = app.add_option("-d,--destination", fbxDestination, "Folder destination or specific FBX File Name (see details above!)");
	CLI::Option* overOpt = app.add_flag("-o,--override-anim-name", "Will use the MSH file name as Animation name, rather than the Animation name stored inside the MSH file.");
	CLI::Option* recOpt = app.add_flag("-r,--recursive", "For all given directories, crawling will be recursive (will include all sub-directories).");

	string filterOptionInfo = "What to ignore. Options are:\n";
	for (auto& it = filterMap.begin(); it != filterMap.end(); ++it)
	{
		filterOptionInfo += "\t\t\t\t" + it->first + "\n";
	}
	app.add_option("-i,--ignore", filter, filterOptionInfo);

	// *parse magic*
	CLI11_PARSE(app, argc, argv);

	bool singleFbxFile = false;
	fs::path filename = fbxDestination.filename();
	if (!fbxDestination.empty())
	{
		if (DescribesDirectory(fbxDestination) && !fs::exists(fbxDestination))
		{
			Log("Given destination directory does not exist!");
			Log(app.help());
			return 0;
		}
		else if (fbxDestination.extension() != ".fbx")
		{
			Log("WARNING: Your desired FBX File Name does not have the required .fbx extension!");
			Log(app.help());
			return 0;
		}
		else
		{
			singleFbxFile = true;
		}
	}

	// Do nothing if no msh files are given
	if (files.size() == 0 && animations.size() == 0 && models.size() == 0)
	{
		Log("No MSH files given!");
		Log(app.help());
		return 0;
	}

	// crawl for all msh files if directories are given
	files = GetFiles(files, ".msh", recOpt->count() > 0);
	animations = GetFiles(animations, ".msh", recOpt->count() > 0);
	models = GetFiles(models, ".msh", recOpt->count() > 0);

	// allow everything by default
	Converter::ModelIgnoreFilter = (EModelPurpose)0;
	for (auto it = filter.begin(); it != filter.end(); ++it)
	{
		auto& filterIT = filterMap.find(*it);
		if (filterIT != filterMap.end())
		{
			// ugly... |= operator does not work here
			Converter::ModelIgnoreFilter = (EModelPurpose)(Converter::ModelIgnoreFilter | filterIT->second);
		}
		else
		{
			Log("'"+*it+"' is not a valid filter option!");
		}
	}

	const bool overrideAnimName = overOpt->count() > 0;
	const size_t numFiles = files.size() + models.size() + animations.size();
	size_t fileCounter = 0;
	size_t successCounter = 0;

	if (singleFbxFile)
	{
		Converter::Start(fbxDestination);
	}

	// Import Models first, ignoring Animations
	Converter::ChunkFilter = EChunkFilter::Animations;
	for (auto& it = models.begin(); it != models.end(); ++it)
	{
		if (!ProcessMSH(*it, overrideAnimName, !singleFbxFile))
		{
			continue;
		}
		ShowProgress((float)(fileCounter++) / numFiles);
		++successCounter;
	}

	// Import complete Files second. These can include both, Models and Animations
	Converter::ChunkFilter = EChunkFilter::None;
	for (auto& it = files.begin(); it != files.end(); ++it)
	{
		if (!ProcessMSH(*it, overrideAnimName, !singleFbxFile))
		{
			continue;
		}
		ShowProgress((float)(fileCounter++) / numFiles);
		++successCounter;
	}

	// Import Animations at last, so all Bones will be there
	Converter::ChunkFilter = EChunkFilter::Models;
	for (auto& it = animations.begin(); it != animations.end(); ++it)
	{
		if (!ProcessMSH(*it, overrideAnimName, !singleFbxFile))
		{
			continue;
		}
		ShowProgress((float)(fileCounter++) / numFiles);
		++successCounter;
	}

	if (singleFbxFile)
	{
		if (successCounter == 0)
		{
			Converter::Close();
		}
		else
		{
			Converter::Save();
		}
	}

	FinishProgress();

#if _DEBUG
	std::cin.get();
#endif
	return 0;
}