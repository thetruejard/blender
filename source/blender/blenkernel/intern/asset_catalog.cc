/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bke
 */

#include "BKE_asset_catalog.hh"

#include "BLI_filesystem.hh"
#include "BLI_string_ref.hh"

#include <filesystem>
#include <fstream>

namespace fs = blender::filesystem;

namespace blender::bke {

const char AssetCatalogService::PATH_SEPARATOR = '/';
const CatalogFilePath AssetCatalogService::DEFAULT_CATALOG_FILENAME = "blender_assets.cats.txt";

AssetCatalogService::AssetCatalogService(const CatalogFilePath &asset_library_root)
    : asset_library_root_(asset_library_root)
{
}

bool AssetCatalogService::is_empty() const
{
  return catalogs_.is_empty();
}

AssetCatalog *AssetCatalogService::find_catalog(CatalogID catalog_id)
{
  std::unique_ptr<AssetCatalog> *catalog_uptr_ptr = this->catalogs_.lookup_ptr(catalog_id);
  if (catalog_uptr_ptr == nullptr) {
    return nullptr;
  }
  return catalog_uptr_ptr->get();
}

void AssetCatalogService::delete_catalog(CatalogID catalog_id)
{
  std::unique_ptr<AssetCatalog> *catalog_uptr_ptr = this->catalogs_.lookup_ptr(catalog_id);
  if (catalog_uptr_ptr == nullptr) {
    /* Catalog cannot be found, which is fine. */
    return;
  }

  /* Mark the catalog as deleted. */
  AssetCatalog *catalog = catalog_uptr_ptr->get();
  catalog->flags.is_deleted = true;

  /* Move ownership from this->catalogs_ to this->deleted_catalogs_. */
  this->deleted_catalogs_.add(catalog_id, std::move(*catalog_uptr_ptr));

  /* The catalog can now be removed from the map without freeing the actual AssetCatalog. */
  this->catalogs_.remove(catalog_id);

  this->rebuild_tree();
}

AssetCatalog *AssetCatalogService::create_catalog(const CatalogPath &catalog_path)
{
  std::unique_ptr<AssetCatalog> catalog = AssetCatalog::from_path(catalog_path);

  /* So we can std::move(catalog) and still use the non-owning pointer: */
  AssetCatalog *const catalog_ptr = catalog.get();

  /* TODO(@sybren): move the `AssetCatalog::from_path()` function to another place, that can reuse
   * catalogs when a catalog with the given path is already known, and avoid duplicate catalog IDs.
   */
  BLI_assert_msg(!catalogs_.contains(catalog->catalog_id), "duplicate catalog ID not supported");
  catalogs_.add_new(catalog->catalog_id, std::move(catalog));

  /* Ensure the new catalog gets written to disk. */
  this->ensure_asset_library_root();
  this->ensure_catalog_definition_file();
  catalog_definition_file_->add_new(catalog_ptr);
  catalog_definition_file_->write_to_disk();

  return catalog_ptr;
}

void AssetCatalogService::ensure_catalog_definition_file()
{
  if (catalog_definition_file_) {
    return;
  }

  auto cdf = std::make_unique<AssetCatalogDefinitionFile>();
  cdf->file_path = asset_library_root_ / DEFAULT_CATALOG_FILENAME;
  catalog_definition_file_ = std::move(cdf);
}

bool AssetCatalogService::ensure_asset_library_root()
{
  /* TODO(@sybren): design a way to get such errors presented to users (or ensure that they never
   * occur). */
  if (asset_library_root_.empty()) {
    std::cerr
        << "AssetCatalogService: no asset library root configured, unable to ensure it exists."
        << std::endl;
    return false;
  }

  if (fs::exists(asset_library_root_)) {
    if (!fs::is_directory(asset_library_root_)) {
      std::cerr << "AssetCatalogService: " << asset_library_root_
                << " exists but is not a directory, this is not a supported situation."
                << std::endl;
      return false;
    }

    /* Root directory exists, work is done. */
    return true;
  }

  /* Ensure the root directory exists. */
  std::error_code err_code;
  if (!fs::create_directories(asset_library_root_, err_code)) {
    std::cerr << "AssetCatalogService: error creating directory " << asset_library_root_ << ": "
              << err_code << std::endl;
    return false;
  }

  /* Root directory has been created, work is done. */
  return true;
}

void AssetCatalogService::load_from_disk()
{
  load_from_disk(asset_library_root_);
}

void AssetCatalogService::load_from_disk(const CatalogFilePath &file_or_directory_path)
{
  fs::file_status status = fs::status(file_or_directory_path);
  switch (status.type()) {
    case fs::file_type::regular:
      load_single_file(file_or_directory_path);
      break;
    case fs::file_type::directory:
      load_directory_recursive(file_or_directory_path);
      break;
    default:
      // TODO(@sybren): throw an appropriate exception.
      return;
  }

  /* TODO: Should there be a sanitize step? E.g. to remove catalogs with identical paths? */

  catalog_tree_ = read_into_tree();
}

void AssetCatalogService::load_directory_recursive(const CatalogFilePath &directory_path)
{
  // TODO(@sybren): implement proper multi-file support. For now, just load
  // the default file if it is there.
  CatalogFilePath file_path = directory_path / DEFAULT_CATALOG_FILENAME;
  fs::file_status fs_status = fs::status(file_path);

  if (!fs::exists(fs_status)) {
    /* No file to be loaded is perfectly fine. */
    return;
  }
  this->load_single_file(file_path);
}

void AssetCatalogService::load_single_file(const CatalogFilePath &catalog_definition_file_path)
{
  /* TODO(@sybren): check that #catalog_definition_file_path is contained in #asset_library_root_,
   * otherwise some assumptions may fail. */
  std::unique_ptr<AssetCatalogDefinitionFile> cdf = parse_catalog_file(
      catalog_definition_file_path);

  BLI_assert_msg(!this->catalog_definition_file_,
                 "Only loading of a single catalog definition file is supported.");
  this->catalog_definition_file_ = std::move(cdf);
}

std::unique_ptr<AssetCatalogDefinitionFile> AssetCatalogService::parse_catalog_file(
    const CatalogFilePath &catalog_definition_file_path)
{
  auto cdf = std::make_unique<AssetCatalogDefinitionFile>();
  cdf->file_path = catalog_definition_file_path;

  std::fstream infile(catalog_definition_file_path);
  std::string line;
  while (std::getline(infile, line)) {
    const StringRef trimmed_line = StringRef(line).trim();
    if (trimmed_line.is_empty() || trimmed_line[0] == '#') {
      continue;
    }

    std::unique_ptr<AssetCatalog> catalog = this->parse_catalog_line(trimmed_line, cdf.get());
    if (!catalog) {
      continue;
    }

    if (cdf->contains(catalog->catalog_id)) {
      std::cerr << catalog_definition_file_path << ": multiple definitions of catalog "
                << catalog->catalog_id << " in the same file, using first occurrence."
                << std::endl;
      /* Don't store 'catalog'; unique_ptr will free its memory. */
      continue;
    }

    if (this->catalogs_.contains(catalog->catalog_id)) {
      // TODO(@sybren): apparently another CDF was already loaded. This is not supported yet.
      std::cerr << catalog_definition_file_path << ": multiple definitions of catalog "
                << catalog->catalog_id << " in multiple files, ignoring this one." << std::endl;
      /* Don't store 'catalog'; unique_ptr will free its memory. */
      continue;
    }

    /* The AssetDefinitionFile should include this catalog when writing it back to disk. */
    cdf->add_new(catalog.get());

    /* The AssetCatalog pointer is owned by the AssetCatalogService. */
    this->catalogs_.add_new(catalog->catalog_id, std::move(catalog));
  }

  return cdf;
}

std::unique_ptr<AssetCatalog> AssetCatalogService::parse_catalog_line(
    const StringRef line, const AssetCatalogDefinitionFile *catalog_definition_file)
{
  const char delim = ':';
  const int64_t first_delim = line.find_first_of(delim);
  if (first_delim == StringRef::not_found) {
    std::cerr << "Invalid line in " << catalog_definition_file->file_path << ": " << line
              << std::endl;
    return std::unique_ptr<AssetCatalog>(nullptr);
  }

  /* Parse the catalog ID. */
  const std::string id_as_string = line.substr(0, first_delim).trim();
  UUID catalog_id;
  const bool uuid_parsed_ok = BLI_uuid_parse_string(&catalog_id, id_as_string.c_str());
  if (!uuid_parsed_ok) {
    std::cerr << "Invalid UUID in " << catalog_definition_file->file_path << ": " << line
              << std::endl;
    return std::unique_ptr<AssetCatalog>(nullptr);
  }

  /* Parse the path and simple name. */
  const StringRef path_and_simple_name = line.substr(first_delim + 1);
  const int64_t second_delim = path_and_simple_name.find_first_of(delim);

  CatalogPath catalog_path;
  std::string simple_name;
  if (second_delim == 0) {
    /* Delimiter as first character means there is no path. These lines are to be ignored. */
    return std::unique_ptr<AssetCatalog>(nullptr);
  }

  if (second_delim == StringRef::not_found) {
    /* No delimiter means no simple name, just treat it as all "path". */
    catalog_path = path_and_simple_name;
    simple_name = "";
  }
  else {
    catalog_path = path_and_simple_name.substr(0, second_delim);
    simple_name = path_and_simple_name.substr(second_delim + 1).trim();
  }

  catalog_path = AssetCatalog::cleanup_path(catalog_path);
  return std::make_unique<AssetCatalog>(catalog_id, catalog_path, simple_name);
}

std::unique_ptr<AssetCatalogTree> AssetCatalogService::read_into_tree()
{
  auto tree = std::make_unique<AssetCatalogTree>();

  /* Go through the catalogs, insert each path component into the tree where needed. */
  for (auto &catalog : catalogs_.values()) {
    /* #fs::path adds useful behavior to the path. Remember that on Windows it uses "\" as
     * separator! For catalogs it should always be "/". Use #fs::path::generic_string if needed. */
    fs::path catalog_path = catalog->path;

    const AssetCatalogTreeItem *parent = nullptr;
    AssetCatalogTreeItem::ChildMap *insert_to_map = &tree->children_;

    BLI_assert_msg(catalog_path.is_relative() && !catalog_path.has_root_path(),
                   "Malformed catalog path: Path should be a relative path, with no root-name or "
                   "root-directory as defined by std::filesystem::path.");
    for (const fs::path &component : catalog_path) {
      std::string component_name = component.string();

      /* Insert new tree element - if no matching one is there yet! */
      auto [item, was_inserted] = insert_to_map->emplace(
          component_name, AssetCatalogTreeItem(component_name, parent));

      /* Walk further into the path (no matter if a new item was created or not). */
      parent = &item->second;
      insert_to_map = &item->second.children_;
    }
  }

  return tree;
}

void AssetCatalogService::rebuild_tree()
{
  this->catalog_tree_ = read_into_tree();
}

AssetCatalogTreeItem::AssetCatalogTreeItem(StringRef name, const AssetCatalogTreeItem *parent)
    : name_(name), parent_(parent)
{
}

StringRef AssetCatalogTreeItem::get_name() const
{
  return name_;
}

CatalogPath AssetCatalogTreeItem::catalog_path() const
{
  std::string current_path = name_;
  for (const AssetCatalogTreeItem *parent = parent_; parent; parent = parent->parent_) {
    current_path = parent->name_ + AssetCatalogService::PATH_SEPARATOR + current_path;
  }
  return current_path;
}

int AssetCatalogTreeItem::count_parents() const
{
  int i = 0;
  for (const AssetCatalogTreeItem *parent = parent_; parent; parent = parent->parent_) {
    i++;
  }
  return i;
}

void AssetCatalogTree::foreach_item(const AssetCatalogTreeItem::ItemIterFn callback) const
{
  AssetCatalogTreeItem::foreach_item_recursive(children_, callback);
}

void AssetCatalogTreeItem::foreach_item_recursive(const AssetCatalogTreeItem::ChildMap &children,
                                                  const ItemIterFn callback)
{
  for (const auto &[key, item] : children) {
    callback(item);
    foreach_item_recursive(item.children_, callback);
  }
}

AssetCatalogTree *AssetCatalogService::get_catalog_tree()
{
  return catalog_tree_.get();
}

bool AssetCatalogDefinitionFile::contains(const CatalogID catalog_id) const
{
  return catalogs_.contains(catalog_id);
}

void AssetCatalogDefinitionFile::add_new(AssetCatalog *catalog)
{
  catalogs_.add_new(catalog->catalog_id, catalog);
}

void AssetCatalogDefinitionFile::write_to_disk() const
{
  this->write_to_disk(this->file_path);
}

void AssetCatalogDefinitionFile::write_to_disk(const CatalogFilePath &file_path) const
{
  // TODO(@sybren): create a backup of the original file, if it exists.
  std::ofstream output(file_path);

  // TODO(@sybren): remember the line ending style that was originally read, then use that to write
  // the file again.

  // Write the header.
  // TODO(@sybren): move the header definition to some other place.
  output << "# This is an Asset Catalog Definition file for Blender." << std::endl;
  output << "#" << std::endl;
  output << "# Empty lines and lines starting with `#` will be ignored." << std::endl;
  output << "# Other lines are of the format \"CATALOG_ID /catalog/path/for/assets\"" << std::endl;
  output << "" << std::endl;

  // Write the catalogs.
  // TODO(@sybren): order them by Catalog ID or Catalog Path.
  for (const auto &catalog : catalogs_.values()) {
    if (catalog->flags.is_deleted) {
      continue;
    }
    output << catalog->catalog_id << ":" << catalog->path << ":" << catalog->simple_name
           << std::endl;
  }
}

AssetCatalog::AssetCatalog(const CatalogID catalog_id,
                           const CatalogPath &path,
                           const std::string &simple_name)
    : catalog_id(catalog_id), path(path), simple_name(simple_name)
{
}

std::unique_ptr<AssetCatalog> AssetCatalog::from_path(const CatalogPath &path)
{
  const CatalogPath clean_path = cleanup_path(path);
  const CatalogID cat_id = BLI_uuid_generate_random();
  const std::string simple_name = sensible_simple_name_for_path(clean_path);
  auto catalog = std::make_unique<AssetCatalog>(cat_id, clean_path, simple_name);
  return catalog;
}

std::string AssetCatalog::sensible_simple_name_for_path(const CatalogPath &path)
{
  std::string name = path;
  std::replace(name.begin(), name.end(), AssetCatalogService::PATH_SEPARATOR, '-');
  if (name.length() < MAX_NAME - 1) {
    return name;
  }

  /* Trim off the start of the path, as that's the most generic part and thus contains the least
   * information. */
  return "..." + name.substr(name.length() - 60);
}

CatalogPath AssetCatalog::cleanup_path(const CatalogPath &path)
{
  /* TODO(@sybren): maybe go over each element of the path, and trim those? */
  CatalogPath clean_path = StringRef(path).trim().trim(AssetCatalogService::PATH_SEPARATOR).trim();
  return clean_path;
}

}  // namespace blender::bke
