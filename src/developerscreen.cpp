#include "developerscreen.hpp"
#include "gui.h"
#include "renderer.hpp"
#include "oddlib/anim.hpp"
#include "oddlib/lvlarchive.hpp"
#include "fmv.hpp"
#include "sound.hpp"
#include "gridmap.hpp"

void DevloperScreen::Init()
{
    
}

void DevloperScreen::Update()
{
    //mFmv.Play("INGRDNT.DDV");
    mFmv.Update();
    mSound.Update();
    mLevel.Update();
}

void DevloperScreen::Render(int w, int h, Renderer& renderer)
{
    //DebugRender();

    // When this gets bigger it can be moved to a separate class etc.
    struct EditorUi
    {
        EditorUi()
            : animationBrowserOpen(true)
        {

        }
        bool resPathsOpen;
        bool fmvBrowserOpen;
        bool soundBrowserOpen;
        bool levelBrowserOpen;
        bool animationBrowserOpen;
        bool guiLayoutEditorOpen;
    };

    static EditorUi editor;

    gui_begin_window(mGui, "Browsers");
    gui_checkbox(mGui, "resPathsOpen|Resource paths", &editor.resPathsOpen);
    gui_checkbox(mGui, "fmvBrowserOpen|FMV browser", &editor.fmvBrowserOpen);
    gui_checkbox(mGui, "soundBrowserOpen|Sound browser", &editor.soundBrowserOpen);
    gui_checkbox(mGui, "levelBrowserOpen|Level browser", &editor.levelBrowserOpen);
    gui_checkbox(mGui, "animationBrowserOpen|Animation browser", &editor.animationBrowserOpen);
    gui_checkbox(mGui, "guiLayoutEditOpen|GUI layout editor", &editor.guiLayoutEditorOpen);

    gui_end_window(mGui);

    if (editor.resPathsOpen)
    {
        mFsOld.DebugUi(*mGui);
    }

    if (editor.fmvBrowserOpen)
    {
        mFmv.Render(renderer, *mGui, w, h);
    }

    if (editor.soundBrowserOpen)
    {
        mSound.Render(mGui, w, h);
    }

    if (editor.levelBrowserOpen)
    {
        mLevel.Render(renderer, *mGui, w, h);
    }

    if (editor.animationBrowserOpen)
    {
        RenderAnimationSelector(renderer);
    }

    if (editor.guiLayoutEditorOpen)
    {
        gui_layout_editor(mGui, "../src/generated_gui_layout.cpp");
    }
}

void DevloperScreen::RenderAnimationSelector(Renderer& renderer)
{
    gui_begin_window(mGui, "Animations");

    // TODO: At least AoPsx fails to find res due to case sensitive file names

    renderer.beginLayer(gui_layer(mGui));
    for (Animation* anim : mLoadedAnims)
    {
        anim->Animate(renderer);
    }
    renderer.endLayer();

    auto res = mResourceLocator.DebugUi(renderer, mGui);
    if (std::get<0>(res))
    {
        const char* resourceName = std::get<0>(res);
        const char* dataSetName = std::get<1>(res);
        bool load = std::get<2>(res);

        Animation* anim = mAnimResourceGroup.Get(resourceName, dataSetName);
        if (anim)
        {
            if (load)
            {
                mLoadedAnims.insert(anim);
            }
            else
            {
                // Don't unload, just remove from rendering list
                mLoadedAnims.erase(anim);
            }
        }

    }
    gui_end_window(mGui);
}
