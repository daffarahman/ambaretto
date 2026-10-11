#pragma once
#include "saved_copy.hpp"

namespace ambaretto {
template<class Design> bool delete_saved(const Design& value, const std::filesystem::path& directory, std::string& status) {
    if (!value.validate(status)) return false;
    std::error_code ec;
    const auto root=std::filesystem::canonical(directory,ec);
    if (ec) {status="Cannot open the saved folder: "+ec.message(); return false;}
    const auto path=saved_path(value,root);
    std::vector<std::filesystem::path> files{path};
    if constexpr (std::is_same_v<Design,City>) {
        const auto progress=root/(value.id+".play");
        const auto info=std::filesystem::symlink_status(progress,ec);
        if (ec && ec!=std::errc::no_such_file_or_directory) {status="Cannot check city progress: "+ec.message(); return false;}
        ec.clear();
        if (std::filesystem::exists(info)) files.push_back(progress);
    }
    for (const auto& file : files) {
        const auto info=std::filesystem::symlink_status(file,ec);
        if (ec || !std::filesystem::is_regular_file(info)) {status="Delete requires a saved file, not a folder or link."; return false;}
    }
    const auto deleted=root/"deleted";
    std::filesystem::create_directory(deleted,ec);
    if (ec || std::filesystem::is_symlink(std::filesystem::symlink_status(deleted,ec)) || ec) {
        status="Cannot create the deleted folder; saved files retained."; return false;
    }
    std::filesystem::path backup;
    for (unsigned number=1;;++number) {
        backup=deleted/(path.filename().string()+"-"+std::to_string(number));
        if (std::filesystem::create_directory(backup,ec)) break;
        if (ec) {status="Cannot create a deletion backup; saved files retained."; return false;}
    }
    for (std::size_t i=0;i<files.size();++i) {
        std::filesystem::rename(files[i],backup/files[i].filename(),ec);
        if (!ec) continue;
        status="Cannot delete: "+ec.message()+". Saved files retained.";
        while (i>0) {
            --i; std::filesystem::rename(backup/files[i].filename(),files[i],ec);
            if (ec) {status="Delete failed; recover files from "+backup.string()+"."; return false;}
        }
        std::filesystem::remove(backup,ec);
        return false;
    }
    status="Deleted "+value.name+". Backup kept in the deleted folder.";
    return true;
}
} // namespace ambaretto
