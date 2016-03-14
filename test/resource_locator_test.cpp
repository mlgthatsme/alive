#include <gmock/gmock.h>
#include <set>
#include "oddlib/stream.hpp"
#include <jsonxx/jsonxx.h>
#include "logger.hpp"
#include "resourcemapper.hpp"

using namespace ::testing;

class MockFileSystem : public IFileSystem
{
public:
    virtual std::unique_ptr<Oddlib::IStream> Open(const char* fileName) override
    {
        // Can't mock unique_ptr return, so mock raw one which the unique_ptr one will call
        return std::unique_ptr<Oddlib::IStream>(OpenProxy(fileName));
    }

    MOCK_METHOD1(OpenProxy, Oddlib::IStream*(const char*));
    MOCK_METHOD1(EnumerateFiles, std::vector<std::string>(const char* ));
    MOCK_METHOD1(Exists, bool(const char*));
};

TEST(ResourceLocator, ResourceLoaderOpen)
{
    MockFileSystem fs;

    ResourceLoader loader(fs);

    loader.Add("C:\\dataset_location1", 1);
    loader.Add("C:\\dataset_location2", 2);

    EXPECT_CALL(fs, OpenProxy(StrEq("C:\\dataset_location1\\SLIGZ.BND")))
        .WillRepeatedly(Return(nullptr));

    EXPECT_CALL(fs, OpenProxy(StrEq("C:\\dataset_location2\\SLIGZ.BND")))
        .Times(1)
        .WillOnce(Return(new Oddlib::Stream(StringToVector("test"))));

    auto stream = loader.Open("SLIGZ.BND");
    ASSERT_NE(nullptr, stream);

    const auto str = stream->LoadAllToString();
    ASSERT_EQ(str, "test");
}

TEST(ResourceLocator, Cache)
{
    ResourceCache cache;

    const size_t resNameHash = StringHash("foo");
    ASSERT_EQ(nullptr, cache.Find<Animation>(resNameHash));
    {
        Resource<Animation> res1(resNameHash, cache, nullptr);

        std::shared_ptr<Animation> cached = cache.Find<Animation>(resNameHash);
        ASSERT_NE(nullptr, cached);
        ASSERT_EQ(cached.get(), res1.Ptr());
    }
    ASSERT_EQ(nullptr, cache.Find<Animation>(resNameHash));
}

TEST(ResourceLocator, ParseResourceMap)
{
    const std::string resourceMapsJson = R"({"anims":[{"blend_mode":1,"name":"SLIGZ.BND_417_1"},{"blend_mode":1,"name":"SLIGZ.BND_417_2"}],"file":"SLIGZ.BND","id":417})";

    MockFileSystem fs;

    EXPECT_CALL(fs, OpenProxy(StrEq("resource_maps.json")))
        .WillRepeatedly(Return(new Oddlib::Stream(StringToVector(resourceMapsJson))));

    ResourceMapper mapper(fs, "resource_maps.json");
    
    const ResourceMapper::AnimMapping* r0 = mapper.Find("I don't exist");
    ASSERT_EQ(nullptr, r0);

    const ResourceMapper::AnimMapping* r1 = mapper.Find("SLIGZ.BND_417_1");
    ASSERT_NE(nullptr, r1);
    ASSERT_EQ("SLIGZ.BND", r1->mFile);
    ASSERT_EQ(417u, r1->mId);
    ASSERT_EQ(1u, r1->mBlendingMode);

    const ResourceMapper::AnimMapping* r2 = mapper.Find("SLIGZ.BND_417_2");
    ASSERT_NE(nullptr, r2);
    ASSERT_EQ("SLIGZ.BND", r2->mFile);
    ASSERT_EQ(417u, r2->mId);
    ASSERT_EQ(1u, r2->mBlendingMode);

}

TEST(ResourceLocator, ParseGameDefinition)
{
    // TODO
    MockFileSystem fs;
    GameDefinition gd(fs, "test_game_definition.json");
}

TEST(ResourceLocator, GameDefinitionDiscovery)
{
    // TODO - enumerating GD's
}

TEST(ResourceLocator, LocateAnimation)
{
    GameDefinition aePc;
    /*
    aePc.mAuthor = "Oddworld Inhabitants";
    aePc.mDescription = "The original PC version of Oddworld Abe's Exoddus";
    aePc.mName = "Oddworld Abe's Exoddus PC";
    aePc.mDataSetName = "AePc";
    */
    
    MockFileSystem fs;

    EXPECT_CALL(fs, OpenProxy(StrEq("C:\\dataset_location1\\SLIGZ.BND")))
        .WillRepeatedly(Return(nullptr));

    EXPECT_CALL(fs, OpenProxy(StrEq("C:\\dataset_location2\\SLIGZ.BND")))
        .Times(1)
        .WillOnce(Return(new Oddlib::Stream(StringToVector("test"))));   // For SLIGZ.BND_417_1, 2nd call should be cached

    ResourceMapper mapper;
    mapper.AddAnimMapping("SLIGZ.BND_417_1", { "SLIGZ.BND", 417, 1 });
    mapper.AddAnimMapping("SLIGZ.BND_417_2", { "SLIGZ.BND", 417, 1 });

    ResourceLocator locator(fs, aePc, std::move(mapper));

    locator.AddDataPath("C:\\dataset_location2", 2);
    locator.AddDataPath("C:\\dataset_location1", 1);
  
    Resource<Animation> resMapped1 = locator.Locate<Animation>("SLIGZ.BND_417_1");
    resMapped1.Reload();

    Resource<Animation> resMapped2 = locator.Locate<Animation>("SLIGZ.BND_417_1");
    resMapped2.Reload();

    // Can explicitly set the dataset to obtain it from a known location
    Resource<Animation> resDirect = locator.Locate<Animation>("SLIGZ.BND_417_1", "AePc");
    resDirect.Reload();
}

TEST(ResourceLocator, LocateAnimationMod)
{
    // TODO: Like LocateAnimation but with mod override with and without original data
}

TEST(ResourceLocator, LocateFmv)
{
    // TODO
}

TEST(ResourceLocator, LocateSound)
{
    // TODO
}

TEST(ResourceLocator, LocateMusic)
{
    // TODO
}

TEST(ResourceLocator, LocateCamera)
{
    // TODO
}

TEST(ResourceLocator, LocatePath)
{
    // TODO
}

class DataPathIdentities
{
public:
    DataPathIdentities(IFileSystem& fs, const char* dataSetsIdsFileName)
    {
        auto stream = fs.Open(dataSetsIdsFileName);
        Parse(stream->LoadAllToString());
    }

    void AddIdentity(const std::string& dataSetName, const std::vector<std::string>& idenfyingFiles)
    {
        mDataPathIds[dataSetName] = idenfyingFiles;
    }

    std::string Identify(IFileSystem& fs, const std::string& path) const
    {
        for (const auto& dataPathId : mDataPathIds)
        {
            bool foundAll = true;
            for (const auto& identifyingFile : dataPathId.second)
            {
                if (!fs.Exists((path + "\\" + identifyingFile).c_str()))
                {
                    foundAll = false;
                    break;
                }
            }
            if (foundAll)
            {
                return dataPathId.first;
            }
        }
        return "";
    }

private:
    std::map<std::string, std::vector<std::string>> mDataPathIds;

    void Parse(const std::string& json)
    {
        jsonxx::Object root;
        root.parse(json);
        if (root.has<jsonxx::Object>("data_set_ids"))
        {
            jsonxx::Object dataSetIds = root.get<jsonxx::Object>("data_set_ids");
            for (const auto& v : dataSetIds.kv_map())
            {
                jsonxx::Object dataSetId = dataSetIds.get<jsonxx::Object>(v.first);
                jsonxx::Array identifyingFiles = dataSetId.get<jsonxx::Array>("files");
                std::vector<std::string> files;
                files.reserve(identifyingFiles.size());
                for (const auto& f : identifyingFiles.values())
                {
                    files.emplace_back(f->get<jsonxx::String>());
                }
                mDataPathIds[v.first] = files;
            }
        }
    }
};


class DataPaths
{
public:
    DataPaths(IFileSystem& fs, const char* dataSetsIdsFileName, const char* dataPathFileName)
        : mIds(fs, dataSetsIdsFileName)
    {
        auto stream = fs.Open(dataPathFileName);
        std::vector<std::string> paths = Parse(stream->LoadAllToString());
        for (const auto& path : paths)
        {
            // TODO: Store the path and its identity
            mIds.Identify(fs, path);
        }
    }

private:
    std::vector<std::string> Parse(const std::string& json)
    {
        std::vector<std::string> paths;
        jsonxx::Object root;
        root.parse(json);
        if (root.has<jsonxx::Array>("paths"))
        {
            jsonxx::Array pathsArray = root.get<jsonxx::Array>("paths");
            for (const auto& path : pathsArray.values())
            {
                paths.emplace_back(path->get<jsonxx::String>());
            }
        }
        return paths;
    }

    // To match to what a game def wants (AePcCd1, AoDemoPsx etc)
    // we use SLUS codes for PSX or if it contains ABEWIN.EXE etc then its AoPc.
    DataPathIdentities mIds;
};


TEST(ResourceLocator, Construct)
{

    MockFileSystem fs;

    const std::string resourceMapsJson = R"(
{
  "data_set_ids" :
  {
    "AoPc": { "files":  [ "AbeWin.exe" ] },
    "AePc": { "files":  [ "Exoddus.exe" ] }
  }
}
)";

    EXPECT_CALL(fs, OpenProxy(StrEq("datasetids.json")))
        .WillRepeatedly(Return(new Oddlib::Stream(StringToVector(resourceMapsJson))));

    const std::string dataSetsJson = R"(
{
 "paths": [
    "F:\\Program Files\\SteamGames\\SteamApps\\common\\Oddworld Abes Exoddus",
    "C:\\data\\Oddworld - Abe's Exoddus (E) (Disc 1) [SLES-01480].bin"
  ]
}
)";

    EXPECT_CALL(fs, OpenProxy(StrEq("datasets.json")))
        .WillRepeatedly(Return(new Oddlib::Stream(StringToVector(dataSetsJson))));

    // Data paths are saved user paths to game data
    // load the list of data paths (if any) and discover what they are
    DataPaths dataPaths(fs, "datasetids.json", "datasets.json");

    /*
    std::vector<GameDefinition> gds;
 
    // load the enumerated "built-in" game defs
    const auto builtInGds = fs.EnumerateFiles("${game_files}\\GameDefinitions");
    for (const auto& file : builtInGds)
    {
        gds.emplace_back(GameDefinition(fs, file.c_str()));
    }

    // load the enumerated "mod" game defs
    const auto modGs = fs.EnumerateFiles("${user_home}\\Alive\\Mods");
    for (const auto& file : modGs)
    {
        gds.emplace_back(GameDefinition(fs, file.c_str()));
    }


    ResourceMapper mapper;

    // Get the user selected game def
    GameDefinition& selected = gds[0];

    // ask for any missing data sets
    //dataPaths.MissingDataSets(selected.RequiredDataSets());

    // TODO: Pass in data paths here instead of calling AddDataPath?
    // create the resource mapper loading the resource maps from the json db
    ResourceLocator resourceLocator(fs, selected, std::move(mapper));
    
    // TODO: Should be a DataPath instance (?)
    // locator.AddDataPath(dataPaths[i], i);

    // TODO: Allow changing at any point, don't set in ctor?
    //resourceLocator.SetGameDefinition(&selected);
    
    // Now we can obtain resources
    Resource<Animation> resMapped1 = resourceLocator.Locate<Animation>("SLIGZ.BND_417_1");
    resMapped1.Reload();
    */
}
