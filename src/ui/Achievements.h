#pragma once
// Başarımlar: the kahvehane's badges (40, in Turkish), each with a name, a one-line description, an icon and, for some, a
// count to reach (3/10). Pure logic, no raylib: App feeds it what happened (an engine's event, a hand or a match that
// ended, the analysis of a match) and celebrates what it returns (the newly unlocked badges); the İstatistik screen's
// "Başarımlar" page draws it. Kept in a small key=value text file next to the record (basarimlar.txt).
//
// Only what the player plays himself counts, by the same rule as the record (App::recording()): App calls
// setLive(recording(), !unattended()) before feeding it. While not `playing`, every trigger is ignored; the one badge
// about watching the Yapay Zeka (watchedAi) only needs an `interactive` session (no --autoplay, snapshot, --ai-chaos).
//
// Event names (event(); the same as the badge ids). App sends the okey ones; the tables send theirs through
// TableGame::noteAchievement (App reads TableGame::achievementEvents() every frame):
//   101 / okey (App)  okey_bitis (finished with the okey), cift_bitis (finished with pairs), elden_bitis (101: opened
//                     and finished in one go), gosterge (klasik okey: showed the gösterge), tam101 (opened with exactly 101)
//   tavla             mars (the player marsed; with level 2 also kurt_mars), katmerli (a katmerli mars, sent with
//                     "mars"), duses (the player rolled 6-6), gulbahar / fevga (the player won a Gülbahar / Fevga match)
//   pişti             pisti, pisti_ustune (a pişti right after another pişti), vale_pisti (Vale on a single Vale)
//   batak             batak13 (all 13 tricks in a hand), ihale8 (the player bid 8 or more and made it)
//   king              king_cezasiz (a ceza deal without a single penalty: 10 needed), rifki_yok (a whole match, at
//                     least one Rıfkı deal, never took the Rıfkı)
//   dama              dama (a piece of the player's was crowned), dama_uclu (three or more pieces taken in one move)
//   altmışaltı        66_kapat (closed the stock and won the hand), 66_kirk (declared the koz marriage, 40)
//   bezik             bezik (declared a bezik), cift_bezik (declared a double bezik, 500)
//   konken            konken_elden (went out in one go without having laid anything before)
#include <array>
#include <string>
#include <vector>

namespace ui {

enum class BadgeIcon {
    Okey,     // a tile with the okey's star
    Pairs,    // two tiles side by side
    Tile,     // a single tile
    Number,   // a tile with "101"
    Checker,  // a tavla checker (pul)
    Checkers, // two stacked checkers
    Dice,     // two dice
    Card,     // a card
    Cards,    // two fanned cards
    Jack,     // a card with a "V"
    Spade,    // the maça suit
    Heart,    // the kupa suit
    Crown,    // a crown (King, the koz marriage)
    Disc,     // a dama piece with a crown ring
    Trophy,
    Star,
    Flame,    // a streak
    People,   // three heads (the regulars)
    Tea,      // an ince belli tea glass
    Calendar,
    Moon,     // night and morning
    Leaf,     // the seasons
    Pencil,   // "Hatalarım"
    Eye,      // watching the Yapay Zeka
    Hourglass,
    Cloud,    // a bad day
    Indicator,   // a tile lit up (the gösterge)
    CheckerStar, // a checker with a gold star (Kurt beaten)
    Rose,        // Gülbahar
    CardEight,   // the eight of spades (an ihale of 8)
    Bezik,       // the maça kızı and the karo vale
    Deck,        // a closed deck (kapatmak)
    Fan,         // three cards fanned (a whole hand laid down)
    Medal,
    Sunrise,
};

struct AchievementDef {
    const char* id;    // stable key (file, events): "pisti_ustune"
    const char* name;  // "Pişti Üstüne Pişti"
    const char* desc;  // one line: "Bir piştinin hemen ardından sen de pişti yap."
    BadgeIcon icon;
    int goal;          // 1: once; more: a count to reach (shown as a progress bar); 0: the number of games (her_oyun)
    int group;         // the page of the screen: 0 taş ve tahta (okey, tavla, dama), 1 kâğıt oyunları, 2 kahvehane
    bool secret;       // name and description hidden until it opens
};

// One finished match the player played himself (App fills it in at the end of the match).
struct MatchEndInfo {
    int game = 0;           // GameKind index
    int level = 1;          // the opponents: 0 Acemi, 1 Usta, 2 Kurt
    bool won = false;       // the player (or his team) won the match
    unsigned beaten = 0;    // bit s (seats 1..3): the regulars who sat as his opponents in a match he won
    int phase = -1;         // the real clock's time of day: 0 sabah, 1 öğle, 2 akşam, 3 gece (w3d::DayPhase)
    int season = -1;        // the real date's season: 0 ilkbahar, 1 yaz, 2 sonbahar, 3 kış (w3d::Season)
    int hour = -1;          // local hour 0..23
};

class Achievements {
public:
    Achievements();

    // ---- the list
    static const AchievementDef& def(int i);
    static int count();
    static int find(const std::string& id);  // -1: unknown

    // ---- what counts (see the top of the file)
    void setLive(bool playing, bool interactive) {
        playing_ = playing;
        interactive_ = interactive || playing;
    }
    bool live() const { return playing_; }
    // The games playable in this build (bit = GameKind index): "Her Telden" asks for a win in each of them.
    void setGames(unsigned mask) { games_ = mask ? mask : 1u; }

    // ---- state
    int goal(int i) const;
    int progress(int i) const;
    bool unlocked(int i) const;
    int unlockedDay(int i) const;            // Achievements::today() of the unlock, -1: locked
    std::string unlockedDate(int i) const;   // "06.10.2026", "" while locked
    int unlockedCount() const;

    // ---- triggers: each returns the badges it just opened (indices, in order; never one already open)
    std::vector<int> event(const std::string& ev, int level = 1, int day = today());
    std::vector<int> handPlayed(int day = today());   // a hand of a counted match ended (the day is a visit day)
    std::vector<int> matchEnd(const MatchEndInfo& m, int day = today());
    std::vector<int> analysis(int mistakes, int day = today()); // "Hatalarım" of a counted match: its mistakes
    std::vector<int> watchedAi(int day = today());    // the Yapay Zeka played a whole match while the player watched
    // low level: `id`'s count goes up by `amount` / to at least `value` (opens at the goal)
    std::vector<int> add(const std::string& id, int amount = 1, int day = today());
    std::vector<int> reach(const std::string& id, int value, int day = today());

    // ---- file (the path is chosen by App; empty: nothing is read or written)
    void load(const std::string& path);
    void save(const std::string& path) const;
    std::string text() const;                 // what save() writes
    void parse(const std::string& text);      // what load() reads

    static int today();                       // local calendar day number (days since 1970-01-01)
    static std::string dateText(int day);     // "06.10.2026"

private:
    std::vector<int> bump(int i, int value, bool absolute, int day);
    std::vector<int> progress_, day_;
    bool playing_ = false, interactive_ = false;
    unsigned games_ = 0x7Fu;   // the seven games of the first version until App says otherwise
    unsigned wonGames_ = 0;    // games with a won match
    unsigned beaten_ = 0;      // regulars beaten (bits 1..3)
    unsigned phases_ = 0;      // bit 0 sabah, bit 1 gece
    unsigned seasons_ = 0;     // bit per season
    int winStreak_ = 0, loseStreak_ = 0;
    int lastDay_ = -1, dayStreak_ = 0;
};

} // namespace ui
