#pragma once
#include <array>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace ambaretto {
enum class CharacterType { Player, NPC, Police };
enum class CharacterSlot { Pelvis, Body, Head, UpperArm, Forearm, Hand, Thigh, Shin, Shoes, Count };
struct CharacterComponent {
    std::string model;
    std::array<float,3> scale{1,1,1}, offset{}, rotation{};
    bool operator==(const CharacterComponent& b) const { return model==b.model && scale==b.scale && offset==b.offset && rotation==b.rotation; }
};
struct CharacterDesign {
    std::string name = "New character";
    CharacterType type = CharacterType::Player;
    std::array<CharacterComponent,static_cast<std::size_t>(CharacterSlot::Count)> parts{};
    bool operator==(const CharacterDesign& b) const { return name==b.name && type==b.type && parts==b.parts; }
    bool validate(std::string& error) const;
    void write(std::ostream& file) const;
    static bool read(std::istream& file, CharacterDesign& design, std::string& error);
    bool save(const std::filesystem::path& directory, std::string& error) const;
    static bool load(const std::filesystem::path& path, CharacterDesign& design, std::string& error);
};
inline constexpr const char* character_slot_names[] = {"Pelvis", "Body", "Head", "Upper arm", "Forearm", "Hand", "Thigh", "Shin", "Shoes"};
inline constexpr const char* character_slot_folders[] = {"body", "body", "head", "hand", "hand", "hand", "leg", "leg", "shoes"};
inline constexpr const char* character_type_names[] = {"Player", "NPC", "Police"};
std::vector<CharacterDesign> saved_characters(const std::filesystem::path& directory, std::string& error);
const CharacterDesign* character_for_type(const std::vector<CharacterDesign>& designs, CharacterType type, std::size_t index = 0);
std::optional<CharacterDesign> character_builder(CharacterDesign design, const std::filesystem::path& directory, const std::string& screenshot = {});
} // namespace ambaretto
