#include <Engine/ResourceTypes/SceneFormats/RSDKSceneReader.h>

#include <Engine/IO/MemoryStream.h>
#include <Engine/Bytecode/ScriptManager.h>
#include <Engine/Bytecode/ScriptEntity.h>
#include <Engine/Bytecode/Compiler.h>
#include <Engine/Diagnostics/Clock.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Hashing/FNV1A.h>
#include <Engine/Hashing/CombinedHash.h>
#include <Engine/Hashing/CRC32.h>
#include <Engine/IO/Compression/ZLibStream.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Includes/HashMap.h>
#include <Engine/Rendering/Software/SoftwareRenderer.h>
#include <Engine/ResourceTypes/ImageFormats/GIF.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Scene.h>

#include <Engine/TextFormats/XML/XMLParser.h>
#include <Engine/Utilities/StringUtils.h>

#define TILE_FLIPX_MASK 0x80000000U
#define TILE_FLIPY_MASK 0x40000000U
#define TILE_COLLA_MASK 0x30000000U
#define TILE_COLLB_MASK 0x0C000000U
#define TILE_COLLC_MASK 0x03000000U
#define TILE_IDENT_MASK 0x00FFFFFFU

Uint32          RSDKSceneReader::Magic = 0x474643;
Uint16*         RSDKSceneReader::ForegroundChunks = NULL;
bool            RSDKSceneReader::Initialized = false;
Uint32          RSDKSceneReader::tileIndex[CHUNKTILE_COUNT];
Uint32          RSDKSceneReader::direction[CHUNKTILE_COUNT];
Uint32          RSDKSceneReader::visualPlane[CHUNKTILE_COUNT];
Uint32          RSDKSceneReader::collisionFlags[2][CHUNKTILE_COUNT];
std::string     RSDKSceneReader::StageConfig_ObjectList[STAGECONFIG_OBJECTSIZE];

static char* ObjectNames = NULL;
static char* PropertyNames = NULL;
static HashMap<const char*>* ObjectHashes = NULL;
static HashMap<const char*>* PropertyHashes = NULL;

static Uint32 HACK_PlayerNameHash = 0;

void RSDKSceneReader::StageConfig_GetColors(const char* filename) {
    MemoryStream* memoryReader;
    ResourceStream* stageConfigReader;
    /*if ((stageConfigReader = ResourceStream::New(filename))) {
        if ((memoryReader = MemoryStream::New(stageConfigReader))) {
            do {
                Uint32 magic = memoryReader->ReadUInt32();
                if (magic != RSDKSceneReader::Magic)
                    break;

                memoryReader->ReadByte(); // useGameObjects

                int objectNameCount = memoryReader->ReadByte();
                for (int i = 0; i < objectNameCount; i++)
                    Memory::Free(memoryReader->ReadHeaderedString());

                int paletteCount = 8;

                Uint8 Color[3];
                for (int i = 0; i < paletteCount; i++) {
                    // Palette Set
                    int bitmap = memoryReader->ReadUInt16();
                    for (int col = 0; col < 16; col++) {
                        if ((bitmap & (1 << col)) != 0) {
                            for (int d = 0; d < 16; d++) {
                                memoryReader->ReadBytes(Color, 3);
                                Graphics::PaletteColors[i][(col << 4) | d] = 0xFF000000U | Color[0] << 16 | Color[1] << 8 | Color[2];
                            }
                            Graphics::ConvertFromARGBtoNative(&Graphics::PaletteColors[i][(col << 4)], 16);
                            Graphics::PaletteUpdated = true;
                        }
                    }
                }

                int wavConfigCount = memoryReader->ReadByte();
                for (int i = 0; i < wavConfigCount; i++) {
                    Memory::Free(memoryReader->ReadHeaderedString());
                    memoryReader->ReadByte();
                }
            } while (false);

            memoryReader->Close();
        }
        stageConfigReader->Close();
    }*/
}
void RSDKSceneReader::GameConfig_GetColors(const char* filename) {
    MemoryStream* memoryReader;
    ResourceStream* gameConfigReader;
    if ((gameConfigReader = ResourceStream::New(filename))) {
        if ((memoryReader = MemoryStream::New(gameConfigReader))) {
            do {
                Uint32 magic = memoryReader->ReadUInt32();
                if (magic != 0x474643)
                    break;

                Memory::Free(memoryReader->ReadHeaderedString());
                Memory::Free(memoryReader->ReadHeaderedString());
                Memory::Free(memoryReader->ReadHeaderedString());

                memoryReader->ReadByte(); // useGameObjects
                memoryReader->ReadUInt16();

                // Common config
                int objectNameCount = memoryReader->ReadByte();
                for (int i = 0; i < objectNameCount; i++)
                    Memory::Free(memoryReader->ReadHeaderedString());

                int paletteCount = 8;

                Uint8 Color[3];
                for (int i = 0; i < paletteCount; i++) {
                    // Palette Set
                    int bitmap = memoryReader->ReadUInt16();
                    for (int col = 0; col < 16; col++) {
                        if ((bitmap & (1 << col)) != 0) {
                            for (int d = 0; d < 16; d++) {
                                memoryReader->ReadBytes(Color, 3);
                                Graphics::PaletteColors[i][(col << 4) | d] = 0xFF000000U | Color[0] << 16 | Color[1] << 8 | Color[2];
                            }
                            Graphics::ConvertFromARGBtoNative(&Graphics::PaletteColors[i][(col << 4)], 16);
                            Graphics::PaletteUpdated = true;
                        }
                    }
                }

                int wavConfigCount = memoryReader->ReadByte();
                for (int i = 0; i < wavConfigCount; i++) {
                    Memory::Free(memoryReader->ReadHeaderedString());
                    memoryReader->ReadByte();
                }
            } while (false);

            memoryReader->Close();
        }
        gameConfigReader->Close();
    }
}
void RSDKSceneReader::LoadObjectList() {
    // Object names
    Stream* r = ResourceStream::New("ObjectList.txt");
    if (!r) return;

    size_t sz = r->Length();
    ObjectNames = (char*)malloc(sz + 1);
    r->ReadBytes(ObjectNames, sz);
    ObjectNames[sz] = 0;

    ObjectHashes = new HashMap<const char*>(CombinedHash::EncryptData, 1024);

    char* nameHead = ObjectNames;
    char* nameStart = ObjectNames;
    while (*nameHead) {
        if (*nameHead == '\r') {
            *nameHead = 0;
            nameHead++;
        }
        if (*nameHead == '\n') {
            *nameHead = 0;

            if (strcmp(nameStart, "Blank Object") == 0)
                ObjectHashes->Put(nameStart, "Blank_Object");
            else if (strcmp(nameStart, "Music") == 0)
                ObjectHashes->Put(nameStart, "MusicObject");
            else
                ObjectHashes->Put(nameStart, nameStart);

            nameHead++;
            nameStart = nameHead;
            continue;
        }
        nameHead++;
    }
    ObjectHashes->Put(nameStart, nameStart);
    HACK_PlayerNameHash = ObjectHashes->HashFunction("Player", 6);

    r->Close();
}

void RSDKSceneReader::LoadPropertyList() {
    // Property Names
    Stream* r = ResourceStream::New("PropertyList.txt");
    if (!r) return;

    size_t sz = r->Length();
    PropertyNames = (char*)malloc(sz + 1);
    r->ReadBytes(PropertyNames, sz);
    PropertyNames[sz] = 0;

    PropertyHashes = new HashMap<const char*>(CombinedHash::EncryptData, 512);

    char* nameHead = PropertyNames;
    char* nameStart = PropertyNames;
    while (*nameHead) {
        if (*nameHead == '\r') {
            *nameHead = 0;
            nameHead++;
        }
        if (*nameHead == '\n') {
            *nameHead = 0;
            PropertyHashes->Put(nameStart, nameStart);
            nameHead++;
            nameStart = nameHead;
            continue;
        }
        nameHead++;
    }
    PropertyHashes->Put(nameStart, nameStart);

    r->Close();
}

bool RSDKSceneReader::Read(const char* filename, const char* parentFolder) {
    Stream* r = ResourceStream::New(filename);
    if (!r) {
        Log::Print(Log::LOG_ERROR, "Couldn't open file '%s'!", filename);
        return false;
    }
    return RSDKSceneReader::Read(r, parentFolder);
}

SceneLayer RSDKSceneReader::ReadLayer(Stream* r) {
    r->ReadByte(); // Ignored Byte

    char* Name = r->ReadHeaderedString();
    Uint8 layerDrawBehavior = r->ReadByte();
    int   DrawGroup = r->ReadByte();
    int   Width = (int)r->ReadUInt16();
    int   Height = (int)r->ReadUInt16();

    SceneLayer layer(Width, Height);
    layer.DrawBehavior = layerDrawBehavior;

    memset(layer.Name, 0, 50);
    strcpy(layer.Name, Name);
    Memory::Free(Name);

    layer.RelativeY = r->ReadInt16();
    layer.ConstantY = (short)r->ReadInt16();

    layer.Flags = 0;

    if (layer.Name[0] == 'F' && layer.Name[1] == 'G')
        layer.Flags |= SceneLayer::FLAGS_COLLIDEABLE;

    if (strcmp(layer.Name, "Move") != 0) {
        layer.Flags |= SceneLayer::FLAGS_REPEAT_X | SceneLayer::FLAGS_REPEAT_Y;
    }

    layer.DrawGroup = DrawGroup & 0xF;
    if (DrawGroup & 0x10)
        layer.Visible = false;

    layer.ScrollInfoCount = (int)r->ReadUInt16();
    layer.ScrollInfos = (ScrollingInfo*)Memory::Malloc(layer.ScrollInfoCount * sizeof(ScrollingInfo));
    for (int g = 0; g < layer.ScrollInfoCount; g++) {
        layer.ScrollInfos[g].RelativeParallax = r->ReadInt16();
        layer.ScrollInfos[g].ConstantParallax = r->ReadInt16();

        layer.ScrollInfos[g].CanDeform = (char)r->ReadByte();
        r->ReadByte();
    }

    Uint16* tileBoys = (Uint16*)malloc(sizeof(Uint16) * Width * Height);

    Uint32 scrollIndexRead = r->ReadCompressed(layer.ScrollIndexes, 16 * layer.HeightData);
    if (scrollIndexRead > 16 * layer.HeightData) {
        Log::Print(Log::LOG_ERROR, "Read more parallax indexes (%u) than buffer (%d) allows!", scrollIndexRead, 16 * layer.HeightData);
    }
    Uint32 tileBoysRead = r->ReadCompressed(tileBoys, sizeof(Uint16) * Width * Height);
    if (tileBoysRead > sizeof(Uint16) * Width * Height) {
        Log::Print(Log::LOG_ERROR, "Read more tile data (%u) than buffer (%d) allows!", tileBoysRead, sizeof(Uint16) * Width * Height);
    }

    //layer.ScrollInfosSplitIndexesCount = 0;

    // Convert to HatchTiles
    int t = 0;
    Uint32* tileRow = &layer.Tiles[0];
    for (int y = 0; y < layer.Height; y++) {
        for (int x = 0; x < layer.Width; x++) {
            tileRow[x] = (tileBoys[t] & 0x3FF);
            tileRow[x] |= (tileBoys[t] & 0x400) << 21; // Flip X
            tileRow[x] |= (tileBoys[t] & 0x800) << 19; // Flip Y
            tileRow[x] |= (tileBoys[t] & 0xC000) << 12; // Collision B
            tileRow[x] |= (tileBoys[t] & 0x3000) << 16; // Collision A
            t++;
        }
        tileRow += layer.WidthData;
    }
    memcpy(layer.TilesBackup, layer.Tiles, layer.DataSize);

    free(tileBoys);

    return layer;
}
bool RSDKSceneReader::Read(Stream* r, const char* parentFolder) {
    //Okay for debug sakes
    Log::Print(Log::LOG_INFO, "Starting scene is at: %s", parentFolder);

    RSDKSceneReader::Initialized = true;
    unsigned char entry[4];
    //Scene::PriorityPerLayer = 16;
    Scene::InitPriorityLists();
    StageConfig_LoadObjects(parentFolder);

    r->Skip(r->ReadByte()); // Skip the stage header
    r->Skip(5); // Skip 5 bytes because whitehead is retarded

    //Foreground size
    int chunk_width = 0;
    int chunk_height = 0;

    //Read the foreground size
    r->ReadBytes(&chunk_width, 1);
    r->ReadBytes(&entry, 1);            //Unused byte
    r->ReadBytes(&chunk_height, 1);
    r->ReadBytes(&entry, 1);            //Unused byte

    Log::Print(Log::LOG_VERBOSE, "Chunk Size: %i x %i", chunk_width, chunk_height);

    //Create foreground chunk
    ForegroundChunks = (Uint16*)malloc(sizeof(Uint16) * chunk_width * chunk_height);
    //But okay, let's actually load stage chunks
    for (int i = 0; i < chunk_width * chunk_height; ++i)
    {
        //Read 2 bytes for chunks[512 is the limit]
        int chunk_to_store = 0;
        r->ReadBytes(&chunk_to_store, 2);

        //Store chunks to the array
        ForegroundChunks[i] = chunk_to_store;
    }

    //Convert 128x chunk sizes to 16x size
    int Width = (chunk_width * 16) / 2;
    int Height = (chunk_height * 16) / 2;

    r->ReadBytes(&entry[0], 2);

    int object_count = entry[0] + (entry[1] << 8);
    Log::Print(Log::LOG_INFO, "Scene object count %i", object_count);

    for (int i = 0; i < object_count; i++)
    {
        //Read object attributes
        r->ReadBytes(&entry, 2);
        unsigned short attribs = (entry[1] << 8) + entry[0];

        //Read object type
        r->ReadBytes(&entry, 1);
        int objectID = entry[0];
        // Log::Print(Log::LOG_INFO, "Object is at %i", entry[0]);

         //Read object's property value
        r->ReadBytes(&entry, 1);
        int propertyValue = entry[0];

        //Read object's position
        r->ReadBytes(&entry, 4);
        float xpos = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        xpos /= 65536.f;
        r->ReadBytes(&entry, 4);
        float ypos = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        ypos /= 65536.f;

        //Load all of object property values
        int state = 0, direction = 0, scale = 0, rotation = 0, drawOrder = 0, priority = 0, alpha = 0, animation = 0, animationSpeed = 0, frame = 0, inkEffect = 0, values[4] = { 0, 0, 0, 0 };

        //Load the state property
        if (attribs & 0x1)
        {
            r->ReadBytes(&entry, 4);
            state = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the direction value
        if (attribs & 0x2)
        {
            r->ReadBytes(&entry, 1);
            direction = entry[0];
        }

        //Load the scale property value
        if (attribs & 0x4)
        {
            r->ReadBytes(&entry, 4);
            scale = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the rotation property value
        if (attribs & 0x8)
        {
            r->ReadBytes(&entry, 4);
            rotation = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the draw order property value
        if (attribs & 0x10)
        {
            r->ReadBytes(&entry, 1);
            drawOrder = entry[0];
        }

        //Load the priority property value
        if (attribs & 0x20)
        {
            r->ReadBytes(&entry, 1);
            priority = entry[0];
        }

        //Load the alpha property value
        if (attribs & 0x40)
        {
            r->ReadBytes(&entry, 1);
            alpha = entry[0];
        }

        //Load the animation property value
        if (attribs & 0x80)
        {
            r->ReadBytes(&entry, 1);
            animation = entry[0];
        }

        //Load the animation speed property value
        if (attribs & 0x100) {
            r->ReadBytes(&entry, 4);
            animationSpeed = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the frame property value
        if (attribs & 0x200) {
            r->ReadBytes(&entry, 1);
            frame = entry[0];
        }

        //Load the ink effect property value
        if (attribs & 0x400) {
            r->ReadBytes(&entry, 1);
            inkEffect = entry[0];
        }

        //Load the variable 0 property value
        if (attribs & 0x800)
        {
            r->ReadBytes(&entry, 4);
            values[0] = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the variable 1 property value
        if (attribs & 0x1000)
        {
            r->ReadBytes(&entry, 4);
            values[1] = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the variable 2 property value
        if (attribs & 0x2000)
        {
            r->ReadBytes(&entry, 4);
            values[2] = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Load the variable 3 property value
        if (attribs & 0x4000)
        {
            r->ReadBytes(&entry, 4);
            values[3] = (entry[3] << 24) + (entry[2] << 16) + (entry[1] << 8) + entry[0];
        }

        //Create objects
        ObjectList* o = Scene::GetStaticObjectList(StageConfig_ObjectList[objectID - 1].c_str());
        if (o->SpawnFunction)
        {
            ScriptEntity* obj = (ScriptEntity*)o->Spawn();
            if (!obj)
                continue;

            obj->X = xpos;
            obj->Y = ypos;
            obj->InitialX = obj->X;
            obj->InitialY = obj->Y;
            obj->List = o;
            Scene::AddStatic(o, obj);
            obj->InitProperties();

            //obj->SlotID = i + Application::ReservedSlotIDs;

            //Apply property values
            obj->Properties->Put("propertyValue", Property::MakeInteger(propertyValue));
            obj->Properties->Put("state", Property::MakeInteger(state));
            obj->Properties->Put("direction", Property::MakeInteger(direction));
            obj->Properties->Put("scale", Property::MakeInteger(scale));
            obj->Properties->Put("rotation", Property::MakeInteger(rotation));
            obj->Properties->Put("drawOrder", Property::MakeInteger(drawOrder));
            obj->Properties->Put("priority", Property::MakeInteger(priority));
            obj->Properties->Put("alpha", Property::MakeInteger(alpha));
            obj->Properties->Put("animation", Property::MakeInteger(animation));
            obj->Properties->Put("animationSpeed", Property::MakeInteger(animationSpeed));
            obj->Properties->Put("frame", Property::MakeInteger(frame));
            obj->Properties->Put("inkEffect", Property::MakeInteger(inkEffect));
            obj->Properties->Put("value0", Property::MakeInteger(values[0]));
            obj->Properties->Put("value1", Property::MakeInteger(values[1]));
            obj->Properties->Put("value2", Property::MakeInteger(values[2]));
            obj->Properties->Put("value3", Property::MakeInteger(values[3]));
        }
    }

    //Stop reading act binary
    r->Close();

    //Let's load the tileset
    LoadTileset(parentFolder);

    //Load stage chunks
    ReadChunks(parentFolder);
    

    for (int a = 0; a < 2; a++)
    {
        //Create the foreground layer
        SceneLayer layer(Width, Height);

        layer.DrawGroup = 1;
        layer.DrawBehavior = 0;
        layer.Visible = true;

        //Name the foreground layer
        layer.Name = StringUtils::Duplicate(a == 0 ? "Foreground Layer Back" : "Foreground Layer Front");

        //Log the created layer
        //Log::Print(Log::LOG_INFO, "The layer '%s' has been created at size of %i x %i", layer.Name, layer.Width, layer.Height);

        layer.RelativeY = 1.0f;
        layer.ConstantY = 0x00;
        layer.Flags = SceneLayer::FLAGS_COLLIDEABLE;
        layer.DrawGroup = 0;
        //return 0;

        // Create parallax data
        layer.ScrollInfoCount = 1;
        layer.ScrollInfos = (ScrollingInfo*)Memory::Malloc(layer.ScrollInfoCount * sizeof(ScrollingInfo));
        for (int g = 0; g < layer.ScrollInfoCount; g++) {
            layer.ScrollInfos[g].RelativeParallax = 1.0f;
            layer.ScrollInfos[g].ConstantParallax = 0x0000;
            layer.ScrollInfos[g].CanDeform = true;
        }

        //Create tile buffer
        size_t layer_size_in_bytes = Width * Height * sizeof(int);
        int* tile_buffer = NULL;
        tile_buffer = (int*)Memory::Calloc(1, layer_size_in_bytes + 4);

        for (int i = 0; i < chunk_width * chunk_height; i++)
        {
            int x, y;
            x = i % chunk_width;
            y = i / chunk_width;

            SplitChunks(layer, ForegroundChunks[i], x, y, Width, Height, a, true);
        }

        memcpy(layer.TilesBackup, layer.Tiles, layer.DataSize);

        Scene::Layers.push_back(layer);
    }

    //Load background
    ReadBackground(parentFolder);

    return true;
}
bool RSDKSceneReader::ReadChunks(const char* parentFolder)
{
    //Get the directory of the chunks file
    char c[4096];
    snprintf(c, sizeof(c), "%s128x128Tiles.bin", parentFolder);

    //Debug shit
    Log::Print(Log::LOG_INFO, "Loading chunks at: %s", c);

    unsigned char entry[3];

    Stream* r = ResourceStream::New(c);


    for (int i = 0; i < CHUNKTILE_COUNT; ++i)
    {
        r->ReadBytes(&entry, 3);

        //Get priority flag
        entry[0] -= ((entry[0] >> 6) << 6);
        visualPlane[i] = (entry[0] >> 4);

        //Get the flip flag
        entry[0] -= 16 * (entry[0] >> 4);
        direction[i] = (entry[0] >> 2);

        //Get tile index
        entry[0] -= 4 * (entry[0] >> 2);
        tileIndex[i] = entry[1] + (entry[0] << 8);

        //Load collision flags
        collisionFlags[0][i] = entry[2] >> 4;
        collisionFlags[1][i] = entry[2] - ((entry[2] >> 4) << 4);

    }


    r->Close();

    return true;
}
void RSDKSceneReader::ReadBackground(const char* parentFolder)
{
    //Get the directory of the chunks file
    char c[4096];
    snprintf(c, sizeof(c), "%sBackgrounds.bin", parentFolder);

    //Debug shit
    Log::Print(Log::LOG_INFO, "Loading backgrounds at: %s", c);

    unsigned char fileBuffer = 0;
    unsigned char layerCount = 0;
    int entryCountH = 0, entryCountV = 0;

    int hParallaxFactor[0x400], hParallaxSpeed[0x400], hParallaxPos[0x400];

    Stream* r = ResourceStream::New(c);

    //Load layer and parallax count
    r->ReadBytes(&layerCount, 1);
    r->ReadBytes(&entryCountH, 1);

    for (int i = 0; i < entryCountH; ++i) {
        r->ReadBytes(&fileBuffer, 1);
        hParallaxFactor[i] = fileBuffer;
        r->ReadBytes(&fileBuffer, 1);
        hParallaxFactor[i] |= fileBuffer << 8;
        r->ReadBytes(&fileBuffer, 1);
        hParallaxSpeed[i] = fileBuffer << 10;
        hParallaxPos[i] = 0;


        r->Skip(1);                             //FileRead(&hParallax.deform[i], 1);
    }


    r->ReadBytes(&entryCountV, 1);
    for (int i = 0; i < entryCountV; ++i) {
        r->ReadBytes(&fileBuffer, 1);
        //vParallax.parallaxFactor[i] = fileBuffer;
        r->ReadBytes(&fileBuffer, 1);
        //vParallax.parallaxFactor[i] |= fileBuffer << 8;

        r->ReadBytes(&fileBuffer, 1);
        //vParallax.scrollSpeed[i] = fileBuffer << 10;

        //vParallax.scrollPos[i] = 0;

        r->Skip(1);                           //r->ReadBytes(&vParallax.deform[i], 1);
    }



    for (int i = 1; i < layerCount + 1; ++i) {
        int ChunkWidth = 0, ChunkHeight = 0;

        //Read stage size
        r->ReadBytes(&fileBuffer, 1);
        ChunkWidth = fileBuffer;
        r->ReadBytes(&fileBuffer, 1);       //Unused
        r->ReadBytes(&fileBuffer, 1);
        ChunkHeight = fileBuffer;
        r->ReadBytes(&fileBuffer, 1);       //Unused
        r->ReadBytes(&fileBuffer, 1);       //Background type
        r->ReadBytes(&fileBuffer, 1);       //Parallax factor
        r->ReadBytes(&fileBuffer, 1);       //Parallax factor 2
        r->ReadBytes(&fileBuffer, 1);       //Scroll speed


        // Read Line Scroll
        unsigned char buf[3];
        int pos = 0;
        while (true)
        {
            r->ReadBytes(&buf[0], 1);
            if (buf[0] == 0xFF)
            {
                r->ReadBytes(&buf[1], 1);
                if (buf[1] == 0xFF)
                {
                    break;
                }
                else
                {
                    r->ReadBytes(&buf[2], 1);
                    int index = buf[1];
                    int cnt = buf[2] - 1;
                    //for (int c = 0; c < cnt; ++c) *lineScrollPtr++ = index;
                }
            }
            else
            {
                //*lineScrollPtr++ = buf[0];
            }
        }

        //Table for chunks layout
        int ChunkLayout[512] = {};

        // Read Layout
        for (int i = 0; i < ChunkWidth * ChunkHeight; ++i)
        {
            r->ReadBytes(&fileBuffer, 1);
            ChunkLayout[i] = fileBuffer;
            r->ReadBytes(&fileBuffer, 1);
            ChunkLayout[i] |= fileBuffer << 8;
        }


        //Convert 128x chunk sizes to 16x size
        int Width = ChunkWidth * 8;
        int Height = ChunkHeight * 8;

        if (ChunkWidth != 0 && ChunkHeight != 0)
        {
            //Create the foreground layer
            SceneLayer layer(Width, Height);

            layer.DrawGroup = 0;
            layer.DrawBehavior = 0;
            layer.Visible = true;

            //Name the foreground layer
            //memset(layer.Name, 0, 50);
            //strcpy(layer.Name, "Background");
            layer.Name = StringUtils::Duplicate("Background");
            //Log the created layer
            //Log::Print(Log::LOG_INFO, "The layer '%s' has been created at size of %i x %i", layer.Name, layer.Width, layer.Height);

            layer.RelativeY = 1.0f;
            layer.ConstantY = 0x00;
            layer.Flags = SceneLayer::FLAGS_COLLIDEABLE;

            // Create parallax data
            layer.ScrollInfoCount = 1;
            layer.ScrollInfos = (ScrollingInfo*)Memory::Malloc(layer.ScrollInfoCount * sizeof(ScrollingInfo));
            for (int g = 0; g < layer.ScrollInfoCount; g++) {
                layer.ScrollInfos[g].RelativeParallax = 1.0f;
                layer.ScrollInfos[g].ConstantParallax = 0x0000;
                layer.ScrollInfos[g].CanDeform = false;
            }

            for (int i = 0; i < ChunkWidth * ChunkHeight; i++)
            {
                int x, y;
                x = i % ChunkWidth;
                y = i / ChunkWidth;

                //Map low priority
                SplitChunks(layer, ChunkLayout[i], x, y, Width, Height, 0, false);

                //Map high priority
                SplitChunks(layer, ChunkLayout[i], x, y, Width, Height, 1, false);
            }

            memcpy(layer.TilesBackup, layer.Tiles, layer.DataSize);

            Scene::Layers.push_back(layer);
        }
    }
    r->Close();
}

void RSDKSceneReader::SplitChunks(SceneLayer layer, int ChunkID, int chunk_x, int chunk_y, int w, int h, int plane, bool loadCollision)
{
    int tile_to_place[8 * 8], direction_to_place[8 * 8], visual_to_place[8 * 8], behaviour_to_place[2][8 * 8];

    for (int i = 0; i < 8 * 8; i++)
    {
        tile_to_place[i] = tileIndex[i + (64 * ChunkID)];
        direction_to_place[i] = direction[i + (64 * ChunkID)];
        visual_to_place[i] = visualPlane[i + (64 * ChunkID)];
        behaviour_to_place[0][i] = collisionFlags[0][i + (64 * ChunkID)];
        behaviour_to_place[1][i] = collisionFlags[1][i + (64 * ChunkID)];
    }

    int a = 0;
    for (int y = 8 * chunk_y; y < 8 * (chunk_y + 1); y++)
    {
        for (int x = 8 * chunk_x; x < 8 * (chunk_x + 1); x++)
        {
            if (plane == visual_to_place[a] || plane == -1)
            {
                //Log::Print(Log::LOG_INFO, "Loading chunks at: %i", tile_to_place[a]);
                Uint32* tile = &layer.Tiles[x + (y << layer.WidthInBits)];

                *tile = tile_to_place[a] & TILE_IDENT_MASK;

                //Tile flipping
                int f = direction_to_place[a];
                switch (f)
                {
                    //Horizontal tile flip
                case 1:
                    *tile |= TILE_FLIPX_MASK;
                    break;

                    //Vertical tile flipping
                case 2:
                    *tile |= TILE_FLIPY_MASK;
                    break;

                    //Flipping in both directions
                case 3:
                    *tile |= TILE_FLIPX_MASK;
                    *tile |= TILE_FLIPY_MASK;
                    break;
                }

                //Plane A collision behaviour
                if (*tile)
                {
                    //Disable collision again
                    //*tile &= ~TILE_COLLA_MASK;

                    switch (behaviour_to_place[0][a])
                    {
                    case 0:
                        *tile |= TILE_COLLA_MASK;
                        break;

                    case 1:
                        *tile |= 1 << 28;
                        break;


                    case 2:
                        *tile |= 2 << 28;
                        break;
                    }
                }

                //Plane B collision behaviour
                if (*tile)
                {
                    //Disable collision again
                    //*tile &= ~TILE_COLLB_MASK;

                    switch (behaviour_to_place[1][a])
                    {
                    case 0:
                        *tile |= TILE_COLLB_MASK;
                        break;

                    case 1:
                        *tile |= 1 << 26;
                        break;


                    case 2:
                        *tile |= 2 << 26;
                        break;
                    }
                }
            }
            a++;
        }
    }
}
void RSDKSceneReader::StageConfig_LoadObjects(const char* parentFolder)
{
    //Get the directory of the chunks file
    char c[4096];
    snprintf(c, sizeof(c), "%sStageConfig.bin", parentFolder);

    //Debug shit
    Log::Print(Log::LOG_INFO, "Stage config loaded at: %s", c);

    //Genius
    unsigned char entry[3];
    unsigned char clr[3];
    unsigned char b;
    char strBuffer[0x100];

    Stream* r = ResourceStream::New(c);




    for (int i = 0x60; i < 0x80; ++i) {
        r->ReadBytes(&clr, 3);
        //SetPaletteEntry(-1, i, clr[0], clr[1], clr[2]);
    }


    r->Skip(2);

    unsigned char stageObjectCount = 0;
    r->ReadBytes(&stageObjectCount, 1);

    Log::Print(0, "Object from the list %i", stageObjectCount);


    for (int i = 0; i < stageObjectCount; ++i)
    {
        r->ReadBytes(&b, 1);
        r->ReadBytes(strBuffer, b);
        strBuffer[b] = 0;

        //Store to the list
        StageConfig_ObjectList[i] = strBuffer;
        Log::Print(0, "Object from the list %s", StageConfig_ObjectList[i].c_str());
    }

    r->Close();
}
bool RSDKSceneReader::LoadTileset(const char* parentFolder) {
    Graphics::UsePalettes = true;

    char filename16x16Tiles[MAX_RESOURCE_PATH_LENGTH];
    snprintf(filename16x16Tiles, sizeof(filename16x16Tiles), "%s16x16Tiles.gif", parentFolder);

    Stream* resourceStream = ResourceStream::New(filename16x16Tiles);
    if (resourceStream != nullptr) {
        GIF* gif;
        bool loadPalette = Graphics::UsePalettes;

        Graphics::UsePalettes = false;
        gif = GIF::Load(resourceStream);
        Graphics::UsePalettes = loadPalette;

        if (gif) {
            if (gif->Colors) {
                for (int p = 0; p < 256; p++) {
                    Graphics::PaletteColors[0][p] = gif->Colors[p];
                }
                Graphics::PaletteUpdated = true;
            }
            delete gif;
        }

        resourceStream->Close();
    }

    ISprite* tileSprite = new ISprite();
    Texture* spriteSheet = tileSprite->AddSpriteSheet(filename16x16Tiles);
    if (!spriteSheet) {
        delete tileSprite;
        return false;
    }

    int cols = spriteSheet->Width / Scene::TileWidth;
    int rows = spriteSheet->Height / Scene::TileHeight;

    tileSprite->ReserveAnimationCount(1);
    tileSprite->AddAnimation("TileSprite", 0, 0, cols * rows);
    for (int i = 0; i < cols * rows; i++) {
        tileSprite->AddFrame(0,
            (i % cols) * Scene::TileWidth,
            (i / cols) * Scene::TileHeight,
            Scene::TileWidth,
            Scene::TileHeight,
            -Scene::TileWidth / 2,
            -Scene::TileHeight / 2);
    }

    TileSpriteInfo info;
    Scene::TileSpriteInfos.clear();
    for (int i = 0; i < cols * rows; i++) {
        info.Sprite = tileSprite;
        info.AnimationIndex = 0;
        info.FrameIndex = i;
        info.TilesetID = Scene::Tilesets.size();
        Scene::TileSpriteInfos.push_back(info);
    }

    tileSprite->RefreshGraphicsID();

    Tileset sceneTileset(tileSprite,
        Scene::TileWidth,
        Scene::TileHeight,
        0,
        0,
        Scene::TileSpriteInfos.size(),
        filename16x16Tiles);
    Scene::Tilesets.push_back(sceneTileset);

    return true;
}