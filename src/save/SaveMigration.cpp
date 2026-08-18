#include "save/SaveMigration.hpp"

namespace SaveMigration {

void migrateLevelData(nbt::Compound& data) {
    std::int64_t version = nbt::integer(data, "BlockcraftSaveVersion", 0);
    if (version < 1) {
        // Older Blockcraft saves mirrored Time into DayTime. If DayTime is
        // absent, preserve the visible clock by seeding it from Time.
        if (nbt::find(data, "DayTime") == nullptr)
            data["DayTime"] = nbt::Tag(nbt::integer(data, "Time", 0));
        version = 1;
    }
    if (version > currentVersion) return;
    data["BlockcraftSaveVersion"] = nbt::Tag(currentVersion);
}

} // namespace SaveMigration
