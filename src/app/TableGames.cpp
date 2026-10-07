// The table games that aren't okey, by kind.
#include "app/TableGame.h"

namespace app {

std::unique_ptr<TableGame> makeBatakTable();
std::unique_ptr<TableGame> makeKingTable();
std::unique_ptr<TableGame> makeTavlaTable();
std::unique_ptr<TableGame> makePistiTable();
std::unique_ptr<TableGame> makeDamaTable(); // Dama
std::unique_ptr<TableGame> makeAltmisaltiTable(); // Altmışaltı
std::unique_ptr<TableGame> makeBezikTable(); // Bezik
std::unique_ptr<TableGame> makeKonkenTable(); // Konken

std::unique_ptr<TableGame> makeTableGame(ui::GameKind kind) {
    switch (kind) {
    case ui::GameKind::Batak: return makeBatakTable();
    case ui::GameKind::King: return makeKingTable();
    case ui::GameKind::Tavla: return makeTavlaTable();
    case ui::GameKind::Pisti: return makePistiTable();
    case ui::GameKind::Dama: return makeDamaTable(); // Dama
    case ui::GameKind::Altmisalti: return makeAltmisaltiTable(); // Altmışaltı
    case ui::GameKind::Bezik: return makeBezikTable(); // Bezik
    case ui::GameKind::Konken: return makeKonkenTable(); // Konken
    default: return nullptr;
    }
}

} // namespace app
