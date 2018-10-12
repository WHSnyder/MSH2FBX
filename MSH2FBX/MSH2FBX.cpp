#include "pch.h"
#include "fbxsdk.h"

namespace MSH2FBX
{
	using namespace LibSWBF2::Logging;
	using namespace LibSWBF2::Chunks::Mesh;
	using namespace LibSWBF2::Types;

	void Log(const char* msg)
	{
		std::cout << msg << std::endl;
	}
	void Log(const string& msg)
	{
		std::cout << msg << std::endl;
	}
	void LogEntry(LoggerEntry entry)
	{
		Log(entry.m_Message.c_str());
	}
	function<void(LoggerEntry)> LogCallback = LogEntry;
	

	bool MODLToFBXMesh(FbxManager* manager, MODL& model, FbxMesh*& mesh)
	{
		mesh = FbxMesh::Create(manager, model.m_Name.m_Text.c_str());

		vector<FbxVector4> vertices;
		vector<FbxVector4> normals;
		vector<FbxVector2> uvs;
		size_t vertexOffset = 0;

		for (size_t i = 0; i < model.m_Geometry.m_Segments.size(); ++i)
		{
			SEGM& segment = model.m_Geometry.m_Segments[i];

			if (segment.m_VertexList.m_Vertices.size() == segment.m_NormalList.m_Normals.size())
			{
				for (size_t j = 0; j < segment.m_VertexList.m_Vertices.size(); ++j)
				{
					Vector3& v = segment.m_VertexList.m_Vertices[j];
					vertices.emplace_back(v.m_X, v.m_Y, v.m_Z);

					Vector3& n = segment.m_NormalList.m_Normals[j];
					normals.emplace_back(n.m_X, n.m_Y, n.m_Z);

					// UVs are optional
					if (j < segment.m_UVList.m_UVs.size())
					{
						Vector2& uv = segment.m_UVList.m_UVs[j];
						uvs.emplace_back(uv.m_X, uv.m_Y);
					}
				}

				segment.m_TriangleList.CalcPolygons();

				for (size_t j = 0; j < segment.m_TriangleList.m_Polygons.size(); ++j)
				{
					auto& poly = segment.m_TriangleList.m_Polygons[j];

					mesh->BeginPolygon((int)segment.m_MaterialIndex.m_MaterialIndex);
					for (size_t k = 0; k < poly.m_VertexIndices.size(); ++k)
					{
						mesh->AddPolygon((int)(poly.m_VertexIndices[k] + vertexOffset));
					}
					mesh->EndPolygon();

				}

				vertexOffset += segment.m_VertexList.m_Vertices.size();
			}
			else
			{
				Log("Inconsistent lengths of vertices and normals in Segment No: " + std::to_string(i));
				mesh->Destroy(true);
				mesh = nullptr;
				return false;
			}
		}

		// Set vertices (control points)
		mesh->InitControlPoints((int)vertices.size());
		FbxVector4* cps = mesh->GetControlPoints();
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			cps[i] = vertices[i];
		}

		auto elementNormal = mesh->CreateElementNormal();
		elementNormal->SetMappingMode(FbxGeometryElement::eByControlPoint);
		elementNormal->SetReferenceMode(FbxGeometryElement::eDirect);

		for (size_t i = 0; i < normals.size(); ++i)
		{
			elementNormal->GetDirectArray().Add(normals[i]);
		}

		return true;
	}

	void Execute()
	{
		//LOG_SetCallbackMethod(&LogCallback);
		Logger::SetLogCallback(LogCallback);

		const char* FILENAME = "pol_icon_disk.msh";

		MSH* msh = MSH::Create();
		msh->ReadFromFile(FILENAME);

		// Overall FBX (memory) manager
		FbxManager* manager = FbxManager::Create();

		// Create FBX Scene
		FbxScene* scene = FbxScene::Create(manager, FILENAME);
		FbxNode* rootNode = scene->GetRootNode();

		for (size_t i = 0; i < msh->m_MSH2.m_Models.size(); ++i)
		{
			MODL& model = msh->m_MSH2.m_Models[i];

			// Create Mesh
			FbxMesh* mesh = nullptr;

			if (!MODLToFBXMesh(manager, msh->m_MSH2.m_Models[i], mesh))
			{
				Log("Failed to convert MSH Model to FBX Mesh. MODL No: " + std::to_string(i) + "  MTYP: " + std::to_string(model.m_ModelType.m_ModelType));
				continue;
			}

			// Create Node and attach Mesh
			FbxNode* meshNode = FbxNode::Create(manager, model.m_Name.m_Text.c_str());
			meshNode->SetNodeAttribute(mesh);
			meshNode->SetShadingMode(FbxNode::eTextureShading);
			rootNode->AddChild(meshNode);
			scene->AddNode(meshNode);
		}

		// Export Scene to FBX
		FbxExporter* exporter = FbxExporter::Create(manager, "");
		FbxIOSettings* settings = FbxIOSettings::Create(manager, IOSROOT);
		manager->SetIOSettings(settings);
		
		if (!exporter->Initialize("test.fbx", -1, manager->GetIOSettings()))
		{
			Log("Initializing export failed!");
		}

		if (!exporter->Export(scene, false))
		{
			Log("Exporting failed!");
		}

		// Free all
		scene->Destroy();
		exporter->Destroy();
		manager->Destroy();

		MSH::Destroy(msh);
	}
}

// the main function must not lie inside a namespace
int main()
{
	MSH2FBX::Execute();
	return 0;
}