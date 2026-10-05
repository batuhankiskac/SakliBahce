// The table games that aren't okey, by kind.
#include "app/TableGame.h"

namespace app {

std::unique_ptr<TableGame> makeBatakTable();
std::unique_ptr<TableGame> makeKingTable();
std::unique_ptr<TableGame> makeTavlaTable();
std::unique_ptr<TableGame> makePistiTable();

std::unique_ptr<TableGame> makeTableGame(ui::GameKind kind) {
    switch (kind) {
    case ui::GameKind::Batak: return makeBatakTable();
    case ui::GameKind::King: return makeKingTable();
    case ui::GameKind::Tavla: return makeTavlaTable();
    case ui::GameKind::Pisti: return makePistiTable();
    default: return nullptr;
    }
}

} // namespace app
