import re, sys
src = open('src/app/App.cpp').read().split('\n')
L = lambda a, b: src[a-1:b]            # 1-indexed inclusive
def find(s, start=1):
    for i in range(start-1, len(src)):
        if src[i].startswith(s): return i+1
    raise Exception('marker not found: '+s)

inc_end = find('namespace app {') - 1
const_a, const_b = find('constexpr int HUMAN'), find('constexpr int MAX_BOT_ACTIONS')
util_a = find('uint64_t mix64(')
look_a = find('Vector3 lookDir(')
fit_c = find('// The HUD lives on a 16:9 canvas')
ai_a = find('// ---------------------------------------------------------------- the Yapay Zeka mode\'s table messages')
set_a = find('// ---------------------------------------------------------------- settings file')
save_a = find('// ---------------------------------------------------------------- the match left unfinished')
fs_a = find('// ---------------------------------------------------------------- frame statistics')
cls_a = find('// ================================================================ the application')
setup_a = find('// ---------------------------------------------------------------- setup')
rec_stats = find('void App::recordStats(')
tick_a = find('void App::tick(')
snap_a = find('// ---------------------------------------------------------------- snapshots')
flow_a = find('// ---------------------------------------------------------------- flow')
apply_a = find('void App::applySettings(')
record_a = find('// ---------------------------------------------------------------- the player\'s record')
hata_a = find('// ---------------------------------------------------------------- hatalarım')
replays_a = find('// Newest first.')
room_a = find('// ---------------------------------------------------------------- the room around us')
resume_a = find('// "Devam Et": the saved match')
guide_a = find('// ---------------------------------------------------------------- rehber')
updflow_a = find('void App::updateFlow(')
aimode_a = find('// ---------------------------------------------------------------- the Yapay Zeka mode\n') if False else find('// ---------------------------------------------------------------- the Yapay Zeka mode', updflow_a)
chaos_fn = find('void App::updateChaos(')
# the comment block above updateChaos: walk back over comment lines
chaos_a = chaos_fn
while src[chaos_a-2].startswith('//'): chaos_a -= 1
bots_a = find('// ---------------------------------------------------------------- bots')
anon_end = find('} // namespace', bots_a)
cli_a = find('// ================================================================ command line')
app_end = find('} // namespace app')

def block(a, b): return '\n'.join(L(a, b)).rstrip('\n') + '\n'

# ---- pieces
const = block(const_a, const_b)
utilA = block(util_a, look_a - 1)            # mix64 clockSeed actionName
debugView = block(look_a, fit_c - 1)         # lookDir viewCamera
utilB = block(fit_c, ai_a - 1)               # fitToCanvas windowCamera2D trim
aimsg = block(ai_a, set_a - 1)
settings = block(set_a, save_a - 1)
save_all = L(save_a, fs_a - 1)
# cut SavedMatch struct out of save
s0 = next(i for i,l in enumerate(save_all) if l.startswith('struct SavedMatch'))
s1 = next(i for i in range(s0, len(save_all)) if save_all[i] == '};')
savedMatch = '\n'.join(save_all[s0:s1+1]) + '\n'
save = '\n'.join(save_all[:s0] + save_all[s1+2:]).rstrip('\n') + '\n'
save = save.replace('bool readSave(SavedMatch& sv, const std::string& from = std::string()) {', 'bool readSave(SavedMatch& sv, const std::string& from) {')
framestats = block(fs_a, cls_a - 1)
cls = block(cls_a, setup_a - 1)
core1 = block(setup_a, rec_stats - 1)
recstats = block(rec_stats, tick_a - 1)
core2 = block(tick_a, snap_a - 1)
snaps = block(snap_a, flow_a - 1)
flow1 = block(flow_a, apply_a - 1)
settingsM = block(apply_a, record_a - 1)
record1 = block(record_a, hata_a - 1)
hata = block(hata_a, replays_a - 1)
replays = block(replays_a, room_a - 1)
room = block(room_a, resume_a - 1)
resume = block(resume_a, guide_a - 1)
guide = block(guide_a, updflow_a - 1)
flow2 = block(updflow_a, aimode_a - 1)
aimode = block(aimode_a, chaos_a - 1)
chaos = block(chaos_a, bots_a - 1)
bots = block(bots_a, anon_end - 1)
cli = block(cli_a, app_end - 1)
cli = cli.replace('std::unique_ptr<App> app = std::make_unique<App>(options);', 'std::unique_ptr<detail::App> app = std::make_unique<detail::App>(options);')

# guide struct/function: file-local in AppFlow (anon namespace)
gi = guide.index('void App::maybeShowGuide(')
gstart = guide.index('struct Guide')
guide_local = guide[gstart:gi]
guide = guide[:gstart] + 'namespace {\n\n' + guide_local.rstrip('\n') + '\n\n} // namespace\n\n' + guide[gi:]

includes = '\n'.join(src[3:inc_end]).strip('\n')   # the #include lines (skip the 3-line head comment)

hdr = f'''#pragma once
// App's insides, shared by the files the application is split into (one class, by topic; no behaviour lives here):
//   App.cpp          setup, the frame loop, the 3D world and the 2D layer, the command line, run()
//   AppFlow.cpp      the match flow: starting / ending matches and hands, the table we sit at, screen actions, the
//                    engine's events, audio routing, the scoreboard, the room's reactions, rehber and İpucu
//   AppBots.cpp      the okey bots' pacing and the Yapay Zeka mode (with its table messages)
//   AppRecord.cpp    the player's record, save / resume ("Devam Et") and Tekrarlar
//   AppSettings.cpp  the settings file and the command line's session-only overrides
//   AppAnalysis.cpp  "Hatalarım" on a worker thread
//   AppDebug.cpp     snapshots and their extra views, frame statistics, --ai-chaos
{includes}

namespace app {{
namespace detail {{

{const}
// ---- shared helpers (defined in the file of their topic)
uint64_t mix64(uint64_t x);                                            // App.cpp
uint64_t clockSeed();
const char* actionName(okey::BotAction::Kind k);
Camera3D fitToCanvas(Camera3D c, float aspect);
Camera2D windowCamera2D(const ui::Viewport& vp);
std::string trim(const std::string& s);
Camera3D viewCamera(const std::string& view, const Camera3D& seat, bool tavla); // AppDebug.cpp
std::string watchText(const okey::GameEvent& e);                       // AppBots.cpp
std::string supportDir();                                              // AppSettings.cpp
std::string statsPath();
std::string memoryPath();
std::string settingsPath();
std::string legacySettingsPath();
void applySettingLine(ui::Settings& s, const std::string& k, const std::string& v);
void loadSettings(ui::Settings& s);
std::string settingsText(const ui::Settings& s);
bool ensureDirOf(const std::string& path);
void saveSettings(const ui::Settings& s);

// ---------------------------------------------------------------- the match left unfinished (kayit.txt)
{savedMatch}
std::string savePath();                                                // AppRecord.cpp
std::string replayDir();
bool readSave(SavedMatch& sv, const std::string& from = std::string());

{framestats}
{cls}
}} // namespace detail
}} // namespace app
'''

def cpp(head, body):
    return f'''{head}
#include "app/AppInternal.h"

namespace app {{
namespace detail {{

{body.rstrip(chr(10))}

}} // namespace detail
}} // namespace app
'''

files = {}
files['App.cpp'] = f'''// SaklıBahçe — the application: window, main loop, match flow, bot pacing, and the wiring between the engine
// (okey::Game / okey::Bot), the 3D world (Room, Characters, Table3D, PlayerCamera), audio and the menu screens.
// Frame structure: DESIGN3D.md §2. This file: setup, the frame loop, the world and the 2D layer, the command line;
// the rest of App is split by topic (AppInternal.h lists the files).
#include "app/AppInternal.h"

namespace app {{
namespace detail {{

{utilA.rstrip(chr(10))}

{utilB.rstrip(chr(10))}

{core1.rstrip(chr(10))}

{core2.rstrip(chr(10))}

}} // namespace detail

{cli.rstrip(chr(10))}

}} // namespace app
'''
files['AppFlow.cpp'] = cpp('// App: the match flow (AppInternal.h).', flow1 + '\n' + room + '\n' + guide + '\n' + flow2)
files['AppBots.cpp'] = cpp('// App: the okey bots\' pacing and the Yapay Zeka mode (AppInternal.h).', aimsg + '\n' + aimode + '\n' + bots)
files['AppRecord.cpp'] = cpp('// App: the player\'s record, save / resume and Tekrarlar (AppInternal.h).', save + '\n' + record1 + '\n' + replays + '\n' + resume)
files['AppSettings.cpp'] = cpp('// App: the settings file and the command line\'s session-only overrides (AppInternal.h).', settings + '\n' + settingsM)
files['AppAnalysis.cpp'] = cpp('// App: "Hatalarım" (AppInternal.h).', hata)
files['AppDebug.cpp'] = cpp('// App: snapshots and their extra views, frame statistics, --ai-chaos (AppInternal.h).',
                            'namespace {\n\n' + debugView.replace('// Extra snapshot cameras', '} // namespace\n\n// Extra snapshot cameras', 1) + '\n' + snaps + '\n' + recstats + '\n' + chaos)
open('src/app/AppInternal.h', 'w').write(hdr)
for n, t in files.items():
    open('src/app/' + n, 'w').write(t)
# sanity: every original line (non-blank) appears once in the outputs
allout = hdr + ''.join(files.values())
missing = [l for l in src[inc_end:app_end] if l.strip() and l.strip() not in ('namespace {', '} // namespace', 'namespace app {', '} // namespace app') and l not in allout]
print('missing lines:', len(missing)); print('\n'.join(missing[:20]))
