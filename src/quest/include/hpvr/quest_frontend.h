#pragma once
#include "hpvr/hp1_gesture.h"
#include "hpvr/quest_vr_settings.h"
#include "hpvr/quest_demo.h"
#include "hpvr/quest_maps.h"
#include "hpvr/quest_house_point_hud.h"
#include "hpvr/quest_dialogue.h"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
namespace hpvr::quest {
struct FrontTexture { std::string name; std::vector<std::uint8_t> rgba; };
struct ReportSandArt {
    std::uint32_t texture{};
    unsigned x{},y{},width{},height{};
    unsigned source_x{},source_y{};
};
struct StoryPage {
    std::array<std::uint32_t,4> tiles{};
    std::string dialogue_name, subtitle;
    wand::Hp1MpegSound voice;
};
struct FrontAssets {
    std::string owl_letter;
    struct BumpSpeech { std::int32_t actor_reference{}; std::vector<std::string> lines; };
    struct MusicCue {
        std::int32_t actor_reference{};
        std::string tag;
        int music_index=-1;
        unsigned volume_percent=100;
    };
    unsigned map_id=0,level_music_index=3;
    std::vector<std::int32_t> non_bean_pickups;
    std::vector<MusicCue> music_cues;
    std::vector<BumpSpeech> bump_speech;
    std::map<std::string,std::string> dialogue_aliases;
    std::vector<FrontTexture> textures;
    std::array<std::uint32_t,6> menu{}, paper{};
    std::array<std::uint32_t,2> logo{};
    std::array<std::uint32_t,6> book{}, folio_secret{}, report{};
    std::array<ReportSandArt,4> report_sand{};
    std::array<std::uint32_t,7> card_atlas{};
    std::array<std::uint32_t,3> tabs{};
    std::array<std::uint32_t,4> bean_pile{};
    std::uint32_t health_full{},health_empty{},bean_counter{},point_badge{},star_icon{};
    std::uint32_t boss_empty{},peeves_health{},malfoy_health{};
    bool has_boss_art=false;
    float health_top=0,health_bottom=1; // Alpha bounds of owned lightning art.
    std::uint32_t missing_small{},arrow_left{},arrow_right{};
    std::uint32_t font{}, white{}, smoke{},card_face{};
    std::vector<StoryPage> story;
    std::vector<wand::Hp1MpegSound> music;
    std::vector<wand::Hp1MpegSound> gameplay_audio; // Ron, basic cast, twins lesson and pickup.
    wand::Hp1PcmSound frog_pickup;
    wand::Hp1PcmSound scroll_pickup;
    wand::Hp1MpegSound card_pickup;
    std::string level_objective;
    std::string error;
};
bool LoadFrontAssets(const std::filesystem::path& root, FrontAssets* out,unsigned map_id=0);
inline std::size_t ExpectedSceneAudioClipCount(const FrontAssets& assets) {
    // Five introductory lines precede the story and gameplay clips. The basic
    // cast has its own channel; the appended frog PCM occupies its count slot.
    return 5 + assets.story.size() + assets.gameplay_audio.size() +
        (assets.map_id == kHogwartsReturnMapId ? 1U : 0U); // Scroll PCM follows the frog.
}
std::string AudioCacheName(const wand::Hp1MpegSound& source, bool stereo=false);
struct ProgressSave {
    unsigned health=100,lesson_passes=0;
    bool frog_taken=false,card_awarded=false,card_taken=false;
    bool peeves_first_hit=false;
    unsigned peeves_phase=0; // 0 waiting, 1 encounter, 2 departure scene, 3 complete.
    bool twins_departed=false;
    bool filch_seen=false;
    unsigned filch_resume_stage=0;
    std::uint64_t generation=0;
    // 0: narrated book, 1: opening cutscene, 2: free movement after opening.
    unsigned phase=0, page=0, quest_stage=0;
    std::array<float,3> player{0,0.815F,0};
    float yaw=0;
    std::array<std::array<float,4>,11> cast{}; // Opening cast, then Filch, Draco, Crabbe, Goyle, Hermione, Quirrell.
    std::vector<std::int32_t> collected_beans; // Stable owned actor references, sorted.
    std::array<float,2> doors{};
    unsigned map_id=kIntroductionMapId; // Stable QuestMapDescriptor ID; actor references are map-local.
    unsigned banked_beans=0; // Beans collected on completed maps.
    unsigned spent_beans=0;
    std::uint32_t earned_cards=0,completed_maps=0;
    std::array<unsigned,4> house_points{}; // Ravenclaw, Hufflepuff, Slytherin, Gryffindor.
    std::array<unsigned,8> lesson_best{},lesson_points{};
    std::vector<std::int32_t> activated_events; // Sorted unique map-local one-shot references.
    unsigned challenge_stars=0;
    std::string graph_state; // Up to 16 KiB of event-reducer state, never executable script.
    std::string world_state; // Up to 32 KiB of map-local actor and mover state.
    std::string charms_state; // Map-local levitation blocks and lesson state.
};
bool TutorialRewardReady(const ProgressSave& progress);
void ApplyTutorialDamage(ProgressSave& progress);
bool ApplyFirstPeevesContact(ProgressSave& progress);
struct RewardApproach {
    bool ready=false, armed=false;
    bool Update(const ProgressSave& progress,float distance,bool visible);
};
bool ReadProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* out);
bool WriteProgress(const std::filesystem::path& directory,unsigned slot,ProgressSave* inout);
enum class FrontScreen { Main, Slots, Slot, Replace, Story, Game, Pause, Stub, Cards, Report, Objective, Vr, Debug, Welcome, DemoEnd, Levels, LevelSlots, LevelStart, Controls, Letter };
enum class FrontAction { None, NewGame, Continue, StoryDone, SaveMenu, SkipScene, Resume, BeginLevel, OpenCommunity, StartSelectedLevel };
inline constexpr unsigned kVrTurningRow=9;
inline constexpr unsigned kVrTurnSpeedRow=10;
inline constexpr unsigned kVrControlsRow=11;
inline constexpr unsigned kVrGpuBoostRow=12;
inline constexpr unsigned kVrMenuRowCount=14;
inline constexpr unsigned kControlsPageCount=6;
inline constexpr float VrMenuRowY(unsigned row){return 110.0F+20.0F*float(row);}
struct FrontQuad {
    float x=0,y=0,w=0,h=0,u=0,v=0,uw=1,vh=1;
    std::uint32_t texture=0, tint=0xffffff;
};
class QuestFrontEnd {
public:
    FrontAssets assets;
    std::filesystem::path saves;
    FrontScreen screen=FrontScreen::Main;
    FrontScreen paused=FrontScreen::Game;
    unsigned selection=0, slot=0, page=0;
    unsigned selected_map=0;
    std::vector<int> refresh_rates;
    unsigned card_page=0;
    unsigned controls_page=0; // Help navigation must not change the story page.
    float page_time=0;
    bool page_voice_started=false;
    std::array<bool,3> occupied{};
    ProgressSave progress;
    VrSettings vr;
    FrontScreen vr_return=FrontScreen::Game;
    unsigned vr_return_selection=0;
    bool vr_save_failed=false;
    bool debug_pinned=false; // Transient overlay, never written to a save.
    void ToggleVrMenu();
    void ShowDemoNotice(bool finished);
    std::vector<FrontQuad> VrValueQuads(int value,bool scale) const;
    std::vector<FrontQuad> VrRefreshQuads(int hz) const;
    std::vector<FrontQuad> VrTurningQuads(bool smooth) const;
    std::vector<FrontQuad> VrGpuBoostQuads(bool enabled) const;
    std::vector<FrontQuad> VrTurnSpeedQuads(int degrees) const;
    std::vector<FrontQuad> VrVoiceStatusQuads(unsigned status) const;
    std::vector<FrontQuad> VoiceAimQuads(unsigned status, bool alohomora=false) const;
    std::string message;
    // Shown as live text on the main menu until the player enters a game, so
    // a failed level can be reported without device logs.
    std::string error_notice;
    void ShowError(std::string text);
    void RefreshSlots();
    FrontAction Input(float move_y,bool confirm,bool back,float move_x=0);
    FrontAction TickStory(float seconds,float voice_duration);
    void BeginStory(unsigned first_page);
    void BeginGame();
    bool Save();
    bool Visible() const {return screen!=FrontScreen::Game;}
    bool VrPanel() const {return screen==FrontScreen::Vr||screen==FrontScreen::Debug||screen==FrontScreen::Controls;}
    bool DemoNotice() const {return screen==FrontScreen::Welcome||screen==FrontScreen::DemoEnd;}
    bool FloatingPanel() const {return VrPanel()||DemoNotice();}
    bool WorldVisible() const {return screen==FrontScreen::Game||DemoNotice()||(VrPanel()&&(vr_return==FrontScreen::Game||vr_return==FrontScreen::Welcome||vr_return==FrontScreen::DemoEnd));}
    bool PausesWorld() const {return !(screen==FrontScreen::Game||(VrPanel()&&vr_return==FrontScreen::Game));}
    bool PausesAudio() const {
        const auto underlying=VrPanel()?vr_return:screen;
        return underlying==FrontScreen::Pause||underlying==FrontScreen::Cards||underlying==FrontScreen::Report;
    }
    std::vector<FrontQuad> Quads(bool include_report_values=true) const;
    unsigned ReportValue(unsigned field) const;
    std::vector<FrontQuad> ReportDigitQuads(unsigned digit,unsigned field,unsigned place) const;
    std::vector<FrontQuad> ReportSandQuads(unsigned fill_pixels,unsigned house) const;
    std::vector<FrontQuad> FolioCardQuads(unsigned card_index) const;
    std::vector<FrontQuad> BeanCounterQuads(unsigned count) const;
    std::vector<FrontQuad> HousePointQuads(int digit=-1,unsigned place=0,unsigned digits=1) const;
    std::vector<FrontQuad> ChallengeStarQuads(unsigned count,bool report=false) const;
    std::vector<FrontQuad> PeevesHealthQuads(unsigned hits_left,bool active) const;
    std::vector<FrontQuad> MalfoyHealthQuads(unsigned hits_left,bool active) const;
    std::vector<FrontQuad> BroomLabelQuads() const;
    // Fields: 0 hoop hits, 1 remaining seconds, 2 stage. Independently cached.
    std::vector<FrontQuad> BroomNumberQuads(unsigned value,unsigned field) const;
    std::vector<FrontQuad> HudQuads(unsigned count,bool show_beans) const;
    std::vector<FrontQuad> StarPickupQuads(unsigned count) const;
    std::vector<FrontQuad> LessonQuads(unsigned passes,bool ready) const;
    std::string DrawKey() const;
private:
    bool confirm_down_=false,back_down_=false,stick_down_=false;
    bool horizontal_down_=false;
};
inline constexpr std::size_t kErrorNoticeColumns=54;
inline constexpr std::size_t kErrorNoticeLines=12;
// Header plus the upper-case notice wrapped for the main-menu font; long
// tokens are split and overflow ends with "...".
std::vector<std::string> ErrorNoticeLines(std::string_view text);
} // namespace hpvr::quest
