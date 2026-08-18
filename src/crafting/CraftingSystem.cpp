#include "crafting/CraftingSystem.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "core/Json.hpp"
#include "items/ItemRegistry.hpp"

namespace {
std::string itemPath(std::string name) {
    if (name.starts_with("minecraft:")) name.erase(0, 10);
    return name;
}
}

CraftingSystem::CraftingSystem(const std::filesystem::path& assetRoot, const ItemRegistry& items)
    : items_(items) {
    const std::filesystem::path root = assetRoot / "assets/minecraft/recipes";
    if (!std::filesystem::exists(root)) throw std::runtime_error("Missing Minecraft 1.12.2 recipes directory: " + root.string());
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        try {
            const JsonValue json = JsonValue::parseFile(entry.path());
            const std::string type = json.stringOr("type", "");
            if (type != "crafting_shaped" && type != "crafting_shapeless") continue;
            Recipe recipe;
            recipe.name = entry.path().stem().string();
            recipe.shaped = type == "crafting_shaped";

            const JsonValue& result = json.at("result");
            const ItemDefinition* outputDef = items_.find(itemPath(result.at("item").asString()));
            if (!outputDef) continue;
            recipe.result.itemId = outputDef->id;
            recipe.result.count = result.intOr("count", 1);
            recipe.result.damage = static_cast<std::uint16_t>(std::max(0, result.intOr("data", 0)));

            if (recipe.shaped) {
                std::vector<std::string> pattern;
                for (const JsonValue& row : json.at("pattern").asArray()) pattern.push_back(row.asString());
                if (pattern.empty()) continue;
                recipe.height = static_cast<int>(pattern.size());
                recipe.width = static_cast<int>(pattern.front().size());
                const auto& key = json.at("key").asObject();
                for (const std::string& row : pattern) {
                    if (static_cast<int>(row.size()) != recipe.width) throw std::runtime_error("Invalid shaped recipe row width");
                    for (char symbol : row) {
                        if (symbol == ' ') recipe.ingredients.emplace_back();
                        else {
                            auto found = key.find(std::string(1, symbol));
                            if (found == key.end()) throw std::runtime_error("Missing shaped recipe key");
                            recipe.ingredients.push_back(parseIngredient(found->second));
                        }
                    }
                }
            } else {
                for (const JsonValue& ingredient : json.at("ingredients").asArray())
                    recipe.ingredients.push_back(parseIngredient(ingredient));
            }
            if (!recipe.result.empty()) recipes_.push_back(std::move(recipe));
        } catch (const std::exception&) {
            // Vanilla's recipe manager reports malformed JSON and continues. The
            // shipped 1.12.2 JAR should not contain malformed recipes, but one
            // unsupported recipe must never prevent the rest from loading.
        }
    }
    std::sort(recipes_.begin(), recipes_.end(), [](const Recipe& a, const Recipe& b){ return a.name < b.name; });
}

CraftingSystem::Ingredient CraftingSystem::parseIngredient(const JsonValue& value) const {
    Ingredient ingredient;
    auto addObject=[&](const JsonValue& object){
        if (!object.isObject()) return;
        const JsonValue* item = object.find("item");
        if (!item || !item->isString()) return;
        if (const ItemDefinition* def=items_.find(itemPath(item->asString())))
            ingredient.push_back({def->id, object.intOr("data", -1)});
    };
    if (value.isArray()) for (const JsonValue& option : value.asArray()) addObject(option);
    else addObject(value);
    return ingredient;
}

bool CraftingSystem::ingredientMatches(const Ingredient& ingredient, const ItemStack& stack) const {
    if (ingredient.empty()) return stack.empty();
    if (stack.empty()) return false;
    return std::any_of(ingredient.begin(), ingredient.end(), [&](const IngredientOption& option){
        return option.itemId == stack.itemId && (option.data < 0 || option.data == 32767 || option.data == stack.damage);
    });
}

bool CraftingSystem::shapedMatches(const Recipe& recipe, const std::vector<ItemStack>& grid,
                                   int width, int height, int offsetX, int offsetY, bool mirror) const {
    for (int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int rx=x-offsetX, ry=y-offsetY;
        const Ingredient* ingredient=nullptr;
        if (rx>=0 && ry>=0 && rx<recipe.width && ry<recipe.height) {
            const int sx=mirror ? recipe.width-rx-1 : rx;
            ingredient=&recipe.ingredients[static_cast<std::size_t>(ry*recipe.width+sx)];
        }
        const ItemStack& stack=grid[static_cast<std::size_t>(y*width+x)];
        if (ingredient) { if(!ingredientMatches(*ingredient,stack)) return false; }
        else if (!stack.empty()) return false;
    }
    return true;
}

bool CraftingSystem::shapelessMatches(const Recipe& recipe, const std::vector<ItemStack>& grid) const {
    std::vector<bool> used(recipe.ingredients.size(), false);
    std::size_t nonEmpty=0;
    for (const ItemStack& stack : grid) {
        if (stack.empty()) continue;
        ++nonEmpty;
        bool found=false;
        for (std::size_t i=0;i<recipe.ingredients.size();++i) if(!used[i] && ingredientMatches(recipe.ingredients[i],stack)) {
            used[i]=true; found=true; break;
        }
        if(!found) return false;
    }
    return nonEmpty==recipe.ingredients.size();
}

CraftingMatch CraftingSystem::match(const std::vector<ItemStack>& grid, int width, int height) const {
    CraftingMatch result;
    if (width<=0 || height<=0 || static_cast<int>(grid.size()) != width*height) return result;
    for (const Recipe& recipe : recipes_) {
        bool matched=false;
        if (recipe.shaped && recipe.width<=width && recipe.height<=height) {
            for(int oy=0;oy<=height-recipe.height && !matched;++oy)
                for(int ox=0;ox<=width-recipe.width && !matched;++ox)
                    matched=shapedMatches(recipe,grid,width,height,ox,oy,false)||shapedMatches(recipe,grid,width,height,ox,oy,true);
        } else if (!recipe.shaped) matched=shapelessMatches(recipe,grid);
        if (!matched) continue;
        result.output=recipe.result;
        result.recipeName=recipe.name;
        result.remainders.reserve(grid.size());
        for(const ItemStack& stack:grid) result.remainders.push_back(remainderFor(stack));
        return result;
    }
    return result;
}

ItemStack CraftingSystem::remainderFor(const ItemStack& consumed) const {
    if (consumed.empty()) return {};
    if (consumed.itemId==335 || consumed.itemId==326 || consumed.itemId==327) return {325,1,0,{}}; // bucket
    if (consumed.itemId==282 || consumed.itemId==413 || consumed.itemId==436) return {281,1,0,{}}; // bowl
    return {};
}

bool CraftingSystem::takeResult(std::vector<ItemStack>& grid, int width, int height, ItemStack& destination,
                                std::vector<ItemStack>* overflowRemainders) const {
    CraftingMatch found=match(grid,width,height);
    if(found.output.empty()) return false;
    const int maxStack=items_.get(found.output.itemId).maxStackSize;
    if(!destination.empty() && !destination.sameItem(found.output)) return false;
    if(!destination.empty() && destination.count+found.output.count>maxStack) return false;
    if(destination.empty()) destination=found.output; else destination.count+=found.output.count;
    for(std::size_t i=0;i<grid.size();++i) {
        if(grid[i].empty()) continue;
        grid[i].shrink(1);
        const ItemStack& rem=found.remainders[i];
        if(rem.empty()) continue;
        if(grid[i].empty()) grid[i]=rem;
        else if (overflowRemainders != nullptr) overflowRemainders->push_back(rem);
    }
    return true;
}
