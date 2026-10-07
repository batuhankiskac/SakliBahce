#pragma once
// 101 kuralları: the Ayarlar "101 kuralları" options onto the okey engine's rules (101 and eşli 101). One place for
// App::startMatch, the analysis (Analysis.cpp okeyRules) and the tests, so a saved / replayed match (its settings
// line) gets exactly the rules it was played with. Rules and choices: docs/kurallar_101.md.
#include "core/Game.h"
#include "ui/Screens.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace app {

constexpr int kAcmaChoices101[4] = {51, 81, 101, 121}; // açma sınırı
constexpr int kAcmayanChoices101[2] = {202, 404};       // açmayan yazar

inline int snap101(int v, const int* choices, int n) {
    int best = choices[0];
    for (int i = 1; i < n; ++i)
        if (std::abs(choices[i] - v) < std::abs(best - v)) best = choices[i];
    return best;
}

inline void apply101Rules(const ui::Settings& st, okey::RulesConfig& cfg) {
    cfg.katlamali = st.katlamali;
    cfg.leftOpenPenalty = st.yandanCeza;
    cfg.openThreshold = snap101(st.y101Acma, kAcmaChoices101, 4);
    cfg.finishMult = (okey::FinishMult)std::clamp(st.y101Kat, 0, 2);
    cfg.unopenedScore = snap101(st.y101Acmayan, kAcmayanChoices101, 2);
    cfg.penaltyJokerDiscard = st.y101OkeyCeza;
    cfg.penaltyPlayableDiscard = st.y101IslekCeza;
    cfg.penaltyReturnLeft = st.y101GeriVer;
    cfg.waitTurnAfterOpening = st.y101Bekle;
}

// Every 101 kuralları option back to its default (before a saved match's own lines are applied).
inline void reset101Rules(ui::Settings& s) {
    const ui::Settings d;
    s.katlamali = d.katlamali;
    s.yandanCeza = d.yandanCeza;
    s.y101Acma = d.y101Acma;
    s.y101Kat = d.y101Kat;
    s.y101Acmayan = d.y101Acmayan;
    s.y101OkeyCeza = d.y101OkeyCeza;
    s.y101IslekCeza = d.y101IslekCeza;
    s.y101GeriVer = d.y101GeriVer;
    s.y101Bekle = d.y101Bekle;
}

// The non-default 101 rules in one short line ("Katlamalı · açma 121 · tek kat"), empty when all are the defaults.
inline std::string summary101(const okey::RulesConfig& rc) {
    std::string out;
    auto add = [&](const std::string& s) { out += (out.empty() ? "" : " \xC2\xB7 ") + s; };
    if (rc.katlamali) add("Katlamalı");
    if (rc.openThreshold != 101) add("açma " + std::to_string(rc.openThreshold));
    if (rc.finishMult == okey::FinishMult::Single) add("tek kat");
    if (rc.finishMult == okey::FinishMult::None) add("katsız");
    if (rc.unopenedScore != 202) add("açmayan " + std::to_string(rc.unopenedScore));
    if (!rc.penaltyJokerDiscard) add("okey cezasız");
    if (!rc.penaltyPlayableDiscard) add("işlek cezasız");
    if (rc.penaltyReturnLeft) add("geri verme cezalı");
    if (!rc.waitTurnAfterOpening) add("açınca işlenir");
    if (!rc.leftOpenPenalty) add("yandan cezasız");
    return out;
}

} // namespace app
