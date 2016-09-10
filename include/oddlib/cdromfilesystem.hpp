#pragma once

#include <string>
#include "string_util.hpp"
#include "oddlib/exceptions.hpp"
#include "oddlib/exceptions.hpp"
#include "oddlib/stream.hpp"
#include "logger.hpp"
#include <cassert>

class InvalidCdImageException : public Oddlib::Exception
{
public:
    explicit InvalidCdImageException(const char* msg)
        : Exception(msg)
    {

    }
};

const u32 kRawSectorSize = 2352;
const u32 kFileSystemStartSector = 16;

class RawCdImage
{
public:
    RawCdImage(const RawCdImage&) = delete;
    RawCdImage& operator = (const RawCdImage&) = delete;

    RawCdImage(const std::string& fileName)
        : mStream(std::make_unique<Oddlib::FileStream>(fileName, Oddlib::IStream::ReadMode::ReadOnly))
    {
        ReadFileSystem();
    }

    RawCdImage(std::vector<u8>&& buffer)
        : mStream(std::make_unique<Oddlib::MemoryStream>(std::move(buffer)))
    {
        ReadFileSystem();
    }

    void LogTree()
    {
        mRoot.Log(1);
    }

    Sint64 FileExists(std::string fileName) const
    {
        CdNormalizePath(fileName);
        auto rec = DoFind(fileName);
        if (!rec)
        {
            return -1;
        }
        return rec->mDr.data_length.little;
    }

    std::unique_ptr<Oddlib::IStream> ReadFile(std::string fileName, bool includeSubheaders)
    {
        CdNormalizePath(fileName);
        const DrWrapper* record = DoFind(fileName);
        if (!record)
        {
            throw Oddlib::Exception("File not found on CD-ROM");
        }
        // The raw cd bin image file stream will be cloned so that 2 open cd
        // files don't stomp on each others seek pos etc.
        return std::make_unique<CdFileStream>(record->mDr, record->mName, *mStream, includeSubheaders);
    }

public:
    struct DrWrapper;

    const DrWrapper* DoFind(std::string fileName) const
    {
        if (fileName.empty())
        {
            return nullptr;
        }

        if (fileName[0] != '\\')
        {
            fileName = "\\" + fileName;
        }
        auto parts = string_util::split(fileName, '\\');
        return mRoot.Find(parts);
    }

    // Each sector is 2352 bytes
#pragma pack(push)
#pragma pack(1)
    struct RawSectorHeader
    {
        u8 mSync[12]; // Sync bytes of 0xFF
        u8 mMin;
        u8 mSecond;
        u8 mFrame; // aka sector number
        u8 mMode;
        u8 mData[2336];
    };

    struct both_endian_32
    {
        u32 little;
        u32 big;
    };

    struct both_endian_16
    {
        u16 little;
        u16 big;
    };

    struct date_time
    {
        u8 year[4];
        u8 month[2];
        u8 day[2];
        u8 hour[2];
        u8 minute[2];
        u8 second[2];
        u8 mil[2];
        u8 gmt;
    };

    struct volume_descriptor
    {
        u8 mType;
        u8 mMagic[5];
        u8 mVersion;
        u8 mUnused;
        u8 mSys_id[32];
        u8 mVol_id[32];
        u8 mUnused2[8];
        both_endian_32 vol_size;
        u8 mUnused3[32];

        both_endian_16 vol_count;
        both_endian_16 vol_index;
        both_endian_16 logical_block_size;
        both_endian_32 path_table_size;

        u32 path_table_location_LSB;
        u32 path_table_optional_location_LSB;
        u32 path_table_location_MSB;
        u32 path_table_optional_location_MSB;

        u8 root_entry[34];

        u8 vol_set_id[128];
        u8 publisher_id[128];
        u8 data_preparer_id[128];
        u8 app_id[128];
        u8 copyright_file[38];
        u8 abstract_file[36];
        u8 biblio_file[37];

        date_time vol_creation;
        date_time vol_modif;
        date_time vol_expiration;
        date_time vol_effective;

        u8 file_structure_version;
        u8 unused4;

        u8 extra_data[512];
        u8 reserved[653];
    };


    struct path_entry
    {
        u8 name_length;
        u8 extended_length;
        u32 location;
        u16 parent;
    };

    struct directory_record
    {
        u8 length;
        u8 extended_length;
        both_endian_32 location;
        both_endian_32 data_length;
        u8 date[7]; //irregular
        u8 flags;
        u8 unit_size;
        u8 gap_size;
        both_endian_16 sequence_number;
        u8 length_file_id; //files end with ;1
        //file id
        //padding
        //system use
    };
    public:
    struct CDXASector
    {
        //uint8_t sync[12];
       // uint8_t header[4];
        struct CDXASubHeader
        {
            uint8_t file_number;
            uint8_t channel;
            uint8_t submode;
            uint8_t coding_info;
            uint8_t file_number_copy;
            uint8_t channel_number_copy;
            uint8_t submode_copy;
            uint8_t coding_info_copy;
        } subheader;
        uint8_t data[2328];
    };
#pragma pack(pop)

    private:

    void CdNormalizePath(std::string& path) const
    {
        string_util::replace_all(path, "/", "\\");
        string_util::replace_all(path, "\\\\", "\\");
    }

    static bool IsMode2Form2(void* data)
    {
        // To tell the different Mode 2s apart you have to examine bytes 16 - 23 of the sector
        // (the first 8 bytes of Mode Data).If bytes 16 - 19 are not the same as 20 - 23, 
        // then it is Mode 2. If they are equal and bit 5 is on(0x20), then it is Mode 2 Form 2. 
        // Otherwise it is Mode 2 Form 1.
        CDXASector* xaHeader = reinterpret_cast<CDXASector*>(data);
        if (xaHeader->subheader.file_number == xaHeader->subheader.file_number_copy &&
            xaHeader->subheader.channel == xaHeader->subheader.channel_number_copy &&
            xaHeader->subheader.submode == xaHeader->subheader.submode_copy &&
            xaHeader->subheader.coding_info == xaHeader->subheader.coding_info_copy &&
            xaHeader->subheader.submode & 0x20
            )
        {
            //  Mode 2 Form 2
            return true;
        }
        else
        {
            //  Mode 2 Form 1
            return false;
        }
    }

    class Sector
    {
    public:
        Sector(unsigned int sectorNumber, Oddlib::IStream& stream)
        {
            const auto pos = kRawSectorSize*sectorNumber;
            stream.Seek(pos);
            stream.ReadBytes(reinterpret_cast<u8*>(&mData), kRawSectorSize);

            RawSectorHeader* rawHeader = reinterpret_cast<RawSectorHeader*>(&mData);
            if (rawHeader->mMode != 2)
            {
                throw InvalidCdImageException(("Only mode 2 sectors supported got (" + std::to_string(rawHeader->mMode) + ")").c_str());
            }

            mMode2Form1 = !IsMode2Form2(&mData.mData);
        }

        u8* DataPtr()
        {
            if (mMode2Form1)
            {
                return &mData.mData[8];
            }
            else
            {
                return &mData.mData[0];
            }
        }

        u8* RawPtr()
        {
            return reinterpret_cast<u8*>(&mData);
        }

        u32 DataLength()
        {
            return mMode2Form1 ? 2048 : 2336;
        }

    private:
        RawSectorHeader mData;
        bool mMode2Form1 = false;
    };

    const char* NamePointer(directory_record* dr)
    {
        return ((const char*)&dr->length_file_id) + 1;
    }

    bool IsDots(directory_record* dr)
    {
        return (dr->length_file_id == 1 && (NamePointer(dr)[0] == 0x0 || NamePointer(dr)[0] == 0x1));
    }

    std::vector<u8> ReadFile(directory_record* dr)
    {
        int dataSize = dr->data_length.little;
        auto dataSector = dr->location.little;

        std::vector<u8> data;
        data.reserve(dataSize);

        do
        {
            //auto sizeToRead = dataSize;
            Sector sector(dataSector++, *mStream);
            //if (sizeToRead > sector.DataLength())
            {
                //sizeToRead = sector.DataLength();
                dataSize -= sector.DataLength();
            }

            const u8* ptr = sector.RawPtr();
            for (size_t i = 0; i < kRawSectorSize; i++)
            {
                data.emplace_back(*ptr);
                ptr++;
            }
        } while (dataSize > 0);
        
        return data;
    }

    class CdFileStream : public Oddlib::IStream
    {
    public:
        CdFileStream(const CdFileStream&) = delete;
        CdFileStream& operator = (const CdFileStream&) = delete;

        CdFileStream(const directory_record& dr, std::string name, Oddlib::IStream& stream, bool includeSubHeaders)
            : mIncludeSubHeader(includeSubHeaders), mDr(dr), mName(name), mStream(stream.Clone())
        {
            mSector = mDr.location.little;
            mStream->Seek(mSector * kRawSectorSize);
            //mSector = 0;
        }

        virtual Oddlib::IStream* Clone() override
        {
            return new CdFileStream(mDr, mName, *mStream, mIncludeSubHeader);
        }

        virtual Oddlib::IStream* Clone(u32 start, u32 size) override
        {
            directory_record subDir = mDr;
            subDir.location.little += start;
            subDir.data_length.little = size * 2048;
            return new 
                CdFileStream(subDir, 
                mName + "sub(L"
                    + std::to_string(subDir.location.little) + "S" 
                    + std::to_string(subDir.data_length.little) + ")",
                *mStream, 
                mIncludeSubHeader);
        }

        virtual void WriteBytes(const u8* /*pSrc*/, size_t /*srcSize*/) override
        {
            TRACE_ENTRYEXIT;
            throw Oddlib::Exception("WriteBytes not implemented");
        }

        virtual void ReadBytes(u8* pDest, size_t destSize) override
        {
            // Raw CD sector reading mode is only used for FMV's we assume that
            // a full sector will be read for each read. This is slightly hacky but it works
            if (mIncludeSubHeader)
            {
                mStream->Seek((mSector * kRawSectorSize) + 16);
                mSector++;

                char subHeader[8] = {};
                mStream->Read(subHeader);

                mPos += 2048;
                mStream->Seek(mStream->Pos() - sizeof(subHeader));
                mStream->ReadBytes(pDest, destSize);
            }
            else
            {
                // Otherwise we need to handle reading of "normal" files
                for (;;)
                {
                    size_t posWithinSector = mStream->Pos() % (kRawSectorSize);
                 
                    // Skip raw header
                    if (posWithinSector == 0)
                    {
                        char subHeader[24] = {};
                        mStream->ReadBytes(reinterpret_cast<u8*>(subHeader), sizeof(subHeader));
                    }
                    else if (posWithinSector >= 24)
                    {
                        posWithinSector -= 24;
                    }

                    assert(posWithinSector <= 2048);

                    // If what we will read is more than the remaining data in this sector
                    if (posWithinSector + destSize > 2048)
                    {
                        int spaceLeft = 2048 - static_cast<int>(posWithinSector);
                        assert(spaceLeft >= 0);

                        int dataLeftAfterRead = static_cast<int>(destSize) - spaceLeft;
                        assert(dataLeftAfterRead >= 0);

                        mPos += spaceLeft;
                        mStream->ReadBytes(pDest, spaceLeft);
                        pDest += spaceLeft;
                        destSize = dataLeftAfterRead;


                        // Span to next sector
                        mSector++;
                        mStream->Seek((mSector + mDr.location.little) * kRawSectorSize);
                        mPos = mSector * 2048;
                        continue;
                    }
                    else if (destSize)
                    {
                        mPos += destSize;
                        mSector = static_cast<u32>(mStream->Pos() / kRawSectorSize);
                        mSector -= mDr.location.little;
                        mStream->ReadBytes(pDest, destSize);
                    }

                    break;
                }
            }
        }

        virtual void Seek(size_t pos) override
        {
            if (!mIncludeSubHeader)
            {
                // Figure out what sector we should be on
                mSector = static_cast<u32>(pos / 2048);

                // The real file pos must be in raw sector sizes, plus the starting sector
                mStream->Seek(((mSector + mDr.location.little) * kRawSectorSize)+24);


                int subSeek = (pos % 2048);
                mStream->Seek(mStream->Pos() + subSeek);

                mPos = pos;
                return;
            }

            if (pos == 0)
            {
                mSector = mDr.location.little;
                mPos = 0;
                mStream->Seek((mSector * kRawSectorSize) + 16);
                return;
            }

            if ((pos % 2048) != 0)
            {
                mPos = 0;
                mSector = static_cast<u32>(pos / 2048);
                mStream->Seek((mSector * kRawSectorSize) + 16);
                return;
            }

            throw std::runtime_error("Seek() not implemented");
        }

        virtual size_t Pos() const override
        {
            if (!mIncludeSubHeader)
            {
                return mPos;
            }
            throw std::runtime_error("Pos() not implemented");
        }

        virtual size_t Size() const override
        {
            return mDr.data_length.little;
        }

        virtual bool AtEnd() const override
        {
           // std::cout << "Pos " << mPos << " VS " << mDr.data_length.little << " SECTOR " << mSector - mDr.location.little << std::endl;
            return mPos == mDr.data_length.little;
        }

        virtual const std::string& Name() const override
        {
            return mName;
        }

        virtual std::string LoadAllToString() override
        {
            throw Oddlib::Exception("LoadAllToString not implemented yet");
        }

    private:
        size_t mPos = 0;
        bool mIncludeSubHeader = false;
        unsigned int mSector = 0;
        directory_record mDr;
        std::string mName;

        // Stream the raw cd bin image file
        std::unique_ptr<Oddlib::IStream> mStream;
    };

private:

    struct Directory;

    void ReadDirectory(directory_record* rec, Directory* d)
    {
        const auto dataSize = rec->data_length.little;
        auto sectorNum = rec->location.little;
        size_t totalDataRead = 0;

        while (totalDataRead != dataSize)
        {
            Sector sector(sectorNum++, *mStream);
            totalDataRead += sector.DataLength();
            directory_record* dr = (directory_record*)sector.DataPtr();
            while (dr->length)
            {
                if (!IsDots(dr))
                {
                    std::string name(NamePointer(dr), dr->length_file_id);
                    //std::cout << name.c_str() << std::endl;
                    if ((dr->flags & 2) && dr->location.little != rec->location.little)
                    {
                        auto dir = std::make_unique<Directory>();
                        dir->mDir.mDr = *dr;
                        dir->mDir.mName = name;
                        d->mChildren.push_back(std::move(dir));
                        ReadDirectory(dr, d->mChildren.back().get());
                    }
                    else
                    {
                        if (string_util::ends_with(name, ";1"))
                        {
                            name = name.substr(0, name.length() - 2);
                        }
                        d->mFiles.emplace_back(DrWrapper{ *dr, name });
                        // ReadFile(dr);
                    }
                }

                char* ptr = reinterpret_cast<char*>(dr);
                dr = reinterpret_cast<directory_record*>(ptr + dr->length);
            }
        }

    }

    void ReadFileSystem()
    {
        volume_descriptor* volDesc = nullptr;
        auto secNum = kFileSystemStartSector - 1;
        do
        {
            secNum++;
            Sector sector(secNum, *mStream);
            volDesc = (volume_descriptor*)sector.DataPtr();
            if (volDesc->mType == 1)
            {
                directory_record* dr = (directory_record*)&volDesc->root_entry[0];
                mRoot.mDir = DrWrapper{ *dr, "" };
                ReadDirectory(dr, &mRoot);
                break;
            }
        } while (volDesc->mType != 1);
    }

    std::unique_ptr<Oddlib::IStream> mStream;
public:
    struct DrWrapper
    {
        directory_record mDr;
        std::string mName;
    };

private:
    struct Directory
    {
        DrWrapper mDir;
        std::vector<DrWrapper> mFiles;
        std::vector<std::unique_ptr<Directory>> mChildren;
        void Log(int level)
        {
            {
                std::string indent(level, '-');
                std::cout << indent.c_str() << "[dir] " << mDir.mName.c_str() << std::endl;
            }

            {
                std::string indent(level + 1, '-');
                for (auto& file : mFiles)
                {
                    std::cout << indent.c_str() << "[file] " << file.mName.c_str() << std::endl;
                }
            }

            for (auto& child : mChildren)
            {
                child->Log(level + 1);
            }

        }

        const DrWrapper* Find(std::deque<std::string>& parts) const
        {
            if (parts.empty())
            {
                return nullptr;
            }

            auto find = parts.front();
            if (string_util::iequals(mDir.mName, find))
            {
                parts.pop_front();
                if (parts.size() > 1)
                {
                    for (auto& child : mChildren)
                    {
                        auto ret = child->Find(parts);
                        if (ret)
                        {
                            return ret;
                        }
                    }
                    return nullptr;
                }
                else
                {
                    find = parts.front();
                    for (const auto& file : mFiles)
                    {
                        if (string_util::iequals(file.mName, find))
                        {
                            return &file;
                        }
                    }
                    return nullptr;
                }
            }
            else
            {
                // Dir name not matching
                return nullptr;
            }


        }
    };

    Directory mRoot;
};
