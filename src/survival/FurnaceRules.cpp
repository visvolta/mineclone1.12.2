#include "survival/FurnaceRules.hpp"

#include <cstdint>

namespace FurnaceRules {
std::optional<SmeltingRecipe> recipe(const ItemStack& in) {
    if (in.empty()) return std::nullopt;
    const auto out=[&](std::uint16_t id,std::uint16_t damage,float xp){ return std::optional<SmeltingRecipe>(SmeltingRecipe{{id,1,damage,{}},xp}); };
    switch(in.itemId) {
        case 4: return out(1,0,0.1F);                         // cobblestone -> stone
        case 12: return out(20,0,0.1F);                       // sand -> glass
        case 14: return out(266,0,1.0F);                      // gold ore
        case 15: return out(265,0,0.7F);                      // iron ore
        case 16: return out(263,0,0.1F);                      // coal ore
        case 17: case 162: return out(263,1,0.15F);           // logs -> charcoal
        case 19: if(in.damage==1) return out(19,0,0.15F); break;
        case 21: return out(351,4,0.2F);                      // lapis ore -> blue dye
        case 56: return out(264,0,1.0F);                      // diamond ore
        case 73: case 74: return out(331,0,0.7F);             // redstone ore
        case 82: return out(172,0,0.35F);                     // clay block
        case 87: return out(405,0,0.1F);                      // netherrack -> nether brick item
        case 98: if(in.damage==0) return out(98,2,0.1F); break;// stone brick -> cracked
        case 129: return out(388,0,1.0F);                     // emerald ore
        case 153: return out(406,0,0.2F);                     // quartz ore
        case 159: return out(static_cast<std::uint16_t>(235 + (in.damage & 15U)),0,0.1F); // glazed terracotta
        case 319: return out(320,0,0.35F);
        case 337: return out(336,0,0.3F);                      // clay ball -> brick
        case 349:
            if(in.damage==0) return out(350,0,0.35F);
            if(in.damage==1) return out(350,1,0.35F);
            break;
        case 363: return out(364,0,0.35F);
        case 365: return out(366,0,0.35F);
        case 392: return out(393,0,0.35F);
        case 411: return out(412,0,0.35F);
        case 423: return out(424,0,0.35F);
        case 432: return out(433,0,0.1F);
        case 81: return out(351,2,0.2F);                       // cactus -> green dye
        // Chainmail and iron equipment recycle to iron nuggets.
        case 302: case 303: case 304: case 305:
        case 256: case 257: case 258: case 267: case 292:
        case 306: case 307: case 308: case 309: case 417:
            return out(452,0,0.1F);
        // Gold equipment recycles to gold nuggets.
        case 283: case 284: case 285: case 286: case 294:
        case 314: case 315: case 316: case 317: case 418:
            return out(371,0,0.1F);
        default: break;
    }
    return std::nullopt;
}

int fuelBurnTime(const ItemStack& fuel) {
    if(fuel.empty()) return 0;
    switch(fuel.itemId) {
        case 263: return 1600; // coal/charcoal
        case 327: return 20000; // lava bucket
        case 369: return 2400; // blaze rod
        case 173: return 16000; // coal block
        case 280: return 100; // stick
        case 65: return 300; // ladder
        case 143: return 100; // wooden button
        case 171: return 67; // carpet
        case 35: return 100; // wool
        case 126: return 150; // wooden slab
        case 333: case 444: case 445: case 446: case 447: case 448: case 449: return 400; // boats
        case 268: case 269: case 270: case 271: case 290: return 200; // wooden tools
        case 261: case 346: return 300; // bow, fishing rod
        case 323: return 200; // sign
        case 324: case 427: case 428: case 429: case 430: case 431: return 200; // wood doors
        default: break;
    }
    // Block items whose material is WOOD. Numeric block IDs cover the 1.12.2
    // wood families used by ordinary survival progression.
    switch(fuel.itemId) {
        case 5: case 6: case 17: case 47: case 53: case 54: case 58: case 85:
        case 107: case 125: case 134: case 135: case 136: case 146: case 162:
        case 163: case 164: case 183: case 184: case 185: case 186: case 187:
        case 188: case 189: case 190: case 191: case 192: return 300;
        default: return 0;
    }
}

bool isFuel(const ItemStack& fuel) { return fuelBurnTime(fuel)>0; }
}
