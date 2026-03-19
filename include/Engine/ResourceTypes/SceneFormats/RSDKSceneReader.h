#ifndef ENGINE_RESOURCETYPES_SCENEFORMATS_RSDKSCENEREADER_H
#define ENGINE_RESOURCETYPES_SCENEFORMATS_RSDKSCENEREADER_H

#include <Engine/IO/Stream.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Types/Entity.h>

#define STAGECONFIG_OBJECTSIZE 128
#define CHUNKTILE_COUNT (8*8) * 512
class RSDKSceneReader {
private:
	static void LoadObjectList();
	static void LoadPropertyList();
	static SceneLayer ReadLayer(Stream* r);
	static void ReadBackground(const char* parentFolder);
	static void SplitChunks(SceneLayer layer, int ChunkID, int chunk_x, int chunk_y, int w, int h, int plane, bool loadCollision);
	static void StageConfig_LoadObjects(const char* parentFolder);
	static bool LoadTileset(const char* parentFolder);

public:
	static Uint32 Magic;
	static bool Initialized;

	static Uint16* ForegroundChunks;

	static Uint32 tileIndex[CHUNKTILE_COUNT];
	static Uint32 direction[CHUNKTILE_COUNT];
	static Uint32 visualPlane[CHUNKTILE_COUNT];
	static Uint32 collisionFlags[2][CHUNKTILE_COUNT];

	static std::string StageConfig_ObjectList[STAGECONFIG_OBJECTSIZE];

	static void StageConfig_GetColors(const char* filename);
	static void GameConfig_GetColors(const char* filename);
	static bool Read(const char* filename, const char* parentFolder);
	static bool ReadObjectDefinition(Stream* r, Entity** objSlots, const int maxObjSlots);
	static bool Read(Stream* r, const char* parentFolder);
	static bool ReadChunks(const char* parentFolder);
};

#endif /* ENGINE_RESOURCETYPES_SCENEFORMATS_RSDKSCENEREADER_H */
