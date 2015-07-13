#include "fmv.hpp"
#include "imgui/imgui.h"
#include "core/audiobuffer.hpp"
#include "gamedata.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif/*__APPLE__*/

class FmvUi
{
private:
    char buf[4096];
    ImGuiTextFilter mFilter;
    int listbox_item_current = 1;
    std::vector<const char*> listbox_items;
public:
    FmvUi()
    {
#ifdef _WIN32
        strcpy(buf, "C:\\Program Files (x86)\\Steam\\SteamApps\\common\\Oddworld Abes Exoddus\\");
#else
        strcpy(buf, "/home/paul/ae_test/");
#endif
    }

    void DrawVideoSelectionUi(std::unique_ptr<Oddlib::Masher>& video, FILE*& fp, SDL_Surface*& videoFrame, const std::string& setName, const std::vector<std::string>& allFmvs)
    {
        std::string name = "Video player (" + setName + ")";
        ImGui::Begin(name.c_str(), nullptr, ImVec2(550, 580), 1.0f, ImGuiWindowFlags_NoCollapse);


        ImGui::InputText("Video path", buf, sizeof(buf));

        mFilter.Draw();



        listbox_items.resize(allFmvs.size());

        int matchingFilter = 0;
        for (size_t i = 0; i < allFmvs.size(); i++)
        {
            if (mFilter.PassFilter(allFmvs[i].c_str()))
            {
                listbox_items[matchingFilter] = allFmvs[i].c_str();
                matchingFilter++;
            }
        }
        ImGui::PushItemWidth(-1);
        ImGui::ListBox("##", &listbox_item_current, listbox_items.data(), matchingFilter, 27);

        if (ImGui::Button("Play", ImVec2(ImGui::GetWindowWidth(), 20)))
        {
            std::string fullPath = std::string(buf) + listbox_items[listbox_item_current];
            std::cout << "Play " << listbox_items[listbox_item_current] << std::endl;
            try
            {
                video = std::make_unique<Oddlib::Masher>(fullPath);
                if (videoFrame)
                {
                    SDL_FreeSurface(videoFrame);
                    videoFrame = nullptr;
                }

                if (video->HasAudio())
                {
                    AudioBuffer::ChangeAudioSpec(video->SingleAudioFrameSizeBytes(), video->AudioSampleRate());
                }

                if (video->HasVideo())
                {
                    videoFrame = SDL_CreateRGBSurface(0, video->Width(), video->Height(), 32, 0, 0, 0, 0);
                    //                    targetFps = video->FrameRate() * 2;
                }

                SDL_ShowCursor(0);
            }
            catch (const Oddlib::Exception& ex)
            {
                // ImGui::Text(ex.what());

                fp = fopen(fullPath.c_str(), "rb");
                AudioBuffer::ChangeAudioSpec(512*4, 37800);

            }
        }

        ImGui::End();
    }
};



struct CDXASector
{
    uint8_t sync[12];
    uint8_t header[4];

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

static const uint8_t m_CDXA_STEREO = 3;


struct PsxVideoFrameHeader
{
    unsigned short int mNumMdecCodes;
    unsigned short int m3800Magic;
    unsigned short int mQuantizationLevel;
    unsigned short int mVersion;
};


struct MasherVideoHeaderWrapper
{
    unsigned int mSectorType; // AKIK
    unsigned int mSectorNumber;

    // The 4 "unknown" / 0x80010160 in psx data is replaced by "AKIK" in PC data
    unsigned int mAkikMagic;

    unsigned short int mSectorNumberInFrame;
    unsigned short int mNumSectorsInFrame;
    unsigned int mFrameNum;
    unsigned int mFrameDataLen;
    unsigned short int mWidth;
    unsigned short int mHeight;

    PsxVideoFrameHeader mVideoFrameHeader;
    unsigned int mNulls;

    unsigned char frame[2016 + 240 + 4 + 4 + 4 + 2 + 2 + 4 + 4 + 2 + 2 + 2 + 2 + 2 + 2 + 4];

    // PsxVideoFrameHeader mVideoFrameHeader2;

    /*
    0000  char   {4}     "AKIK"

    // This is standard STR format data here


    0004  uint16 {2}     Sector number within frame (zero-based)
    0006  uint16 {2}     Number of sectors in frame
    0008  uint32 {4}     Frame number within file (one-based)
    000C  uint32 {4}     Frame data length
    0010  uint16 {2}     Video Width
    0012  uint16 {2}     Video Height

    0014  uint16 {2}     Number of MDEC codes divided by two, and rounded up to a multiple of 32
    0016  uint16 {2}     Always 0x3800
    0018  uint16 {2}     Quantization level of the video frame
    001A  uint16 {2}     Version of the video frame (always 2)
    001C  uint32 {4}     Always 0x00000000
    */

    //demultiplexing is joining all of the frame data into one buffer without the headers

};

std::vector<Uint32> pixels;

std::vector<unsigned char> ReadFrame(FILE* fp, bool& end, PSXMDECDecoder& mdec, PSXADPCMDecoder& adpcm, bool firstFrame, int& frameW, int& frameH)
{
    std::vector<unsigned char> r(32768);
    std::vector<unsigned char> outPtr(32678);

    unsigned int numSectorsToRead = 0;
    unsigned int sectorNumber = 0;
    const int kDataSize = 2016;
;

    for (;;)
    {
        MasherVideoHeaderWrapper w;
       
        if (fread(&w, 1, sizeof(w), fp) != sizeof(w))
        {
            end = true;
            return r;
        }

        if (w.mSectorType != 0x52494f4d)
        {
            // There is probably no way this is correct
            CDXASector* xa = (CDXASector*)&w;


            auto numBytes = adpcm.DecodeFrameToPCM((int8_t *)outPtr.data(), xa->header, true);

            AudioBuffer::SendSamples((char*)outPtr.data(), numBytes);

            // Must be VALE
            continue;
        }
        else
        {
            frameW = w.mWidth;
            frameH = w.mHeight;

            //std::cout << "sector: " << w.mSectorNumber << std::endl;
            //std::cout << "data len: " << w.mFrameDataLen << std::endl;
            //std::cout << "frame number: " << w.mFrameNum << std::endl;
            //std::cout << "num sectors in frame: " << w.mNumSectorsInFrame << std::endl;
            //std::cout << "frame sector number: " << w.mSectorNumberInFrame << std::endl;
            // SetSurfaceSize(w.mWidth, w.mHeight);


            uint32_t bytes_to_copy = w.mFrameDataLen - w.mSectorNumberInFrame *kDataSize;

            
            if (bytes_to_copy > 0)
            {
                if (bytes_to_copy > kDataSize)
                {
                    bytes_to_copy = kDataSize;
                }

                memcpy(r.data() + w.mSectorNumberInFrame * kDataSize, w.frame, bytes_to_copy);
            }

            if (w.mSectorNumberInFrame == w.mNumSectorsInFrame - 1)
            {
                break;
            }
        }
    }

    if (pixels.empty())
    {
        pixels.resize(frameW * frameH);

    }

    mdec.DecodeFrameToABGR32((uint16_t*)pixels.data(), (uint16_t*)r.data(), frameW, frameH, false);

    SDL_Delay(50);
    return r;
}

void Fmv::RenderVideoUi()
{

    if (!video && !mFp)
    {
        if (mFmvUis.empty())
        {
            auto fmvs = mGameData.Fmvs();
            for (auto fmvSet : fmvs)
            {
                mFmvUis.emplace_back(std::make_unique<FmvUi>());
            }
        }

        if (!mFmvUis.empty())
        {
            int i = 0;
            auto fmvs = mGameData.Fmvs();
            for (auto fmvSet : fmvs)
            {
                mFmvUis[i]->DrawVideoSelectionUi(video, mFp, videoFrame, fmvSet.first, fmvSet.second);
                i++;
            }
        }

    }
    else
    {
        if (mFp)
        {
            bool firstFrame = true;
            bool end = false;
            std::vector<unsigned char> frameData = ReadFrame(mFp, end, mMdec, mAdpcm, firstFrame, frameW, frameH);
            firstFrame = false;
            if (end)
            {
                fclose(mFp);
                mFp = nullptr;
            }
        }
        else if (video)
        {
            std::vector<Uint16> decodedFrame(video->SingleAudioFrameSizeBytes() * 2); // *2 if stereo

            if (!video->Update((Uint32*)videoFrame->pixels, (Uint8*)decodedFrame.data()))
            {
                SDL_ShowCursor(1);
                video = nullptr;
                if (videoFrame)
                {
                    SDL_FreeSurface(videoFrame);
                    videoFrame = nullptr;
                }
                //targetFps = 60;
            }
            else
            {
                AudioBuffer::SendSamples((char*)decodedFrame.data(), decodedFrame.size() * 2);
                while (AudioBuffer::mPlayedSamples < video->FrameNumber() * video->SingleAudioFrameSizeBytes())
                {

                }

            }
        }
    }
}

Fmv::Fmv(GameData& gameData)
    : mGameData(gameData)
{

}

Fmv::~Fmv()
{

}

void Fmv::Play()
{

}

void Fmv::Stop()
{
    SDL_ShowCursor(1);
    video = nullptr;
    if (videoFrame)
    {
        SDL_FreeSurface(videoFrame);
        videoFrame = nullptr;
    }
}

void Fmv::Update()
{

}

void Fmv::Render()
{
    glEnable(GL_TEXTURE_2D);

    RenderVideoUi();

    if (videoFrame || mFp)
    {
        // TODO: Optimize - should use VBO's & update 1 texture rather than creating per frame
        GLuint TextureID = 0;

        glGenTextures(1, &TextureID);
        glBindTexture(GL_TEXTURE_2D, TextureID);

        int Mode = GL_RGB;
        /*
        if (videoFrame->format->BytesPerPixel == 4) 
        {
            Mode = GL_RGBA;
        }
        */

        if (videoFrame)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, videoFrame->w, videoFrame->h, 0, GL_RGB, GL_UNSIGNED_BYTE, videoFrame->pixels);
        }
        else if (mFp)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameW, frameH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


        // For Ortho mode, of course
        int X = -1;
        int Y = -1;
        int Width = 2;
        int Height = 2;

        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(X, Y, 0);
        glTexCoord2f(1, 0); glVertex3f(X + Width, Y, 0);
        glTexCoord2f(1, -1); glVertex3f(X + Width, Y + Height, 0);
        glTexCoord2f(0, -1); glVertex3f(X, Y + Height, 0);
        glEnd();

        glDeleteTextures(1, &TextureID);
    }


}
