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

#ifndef __BKE_NODE_TREE_REF_HH__
#define __BKE_NODE_TREE_REF_HH__

#include "BLI_linear_allocator.hh"
#include "BLI_string_map.hh"
#include "BLI_string_ref.hh"
#include "BLI_utility_mixins.hh"
#include "BLI_vector.hh"

#include "DNA_node_types.h"

#include "RNA_access.h"

namespace BKE {

using BLI::ArrayRef;
using BLI::LinearAllocator;
using BLI::StringMap;
using BLI::StringRef;
using BLI::StringRefNull;
using BLI::Vector;

class SocketRef;
class InputSocketRef;
class OutputSocketRef;
class NodeRef;
class NodeTreeRef;

class SocketRef : BLI::NonCopyable, BLI::NonMovable {
 protected:
  NodeRef *m_node;
  bNodeSocket *m_bsocket;
  bool m_is_input;
  uint m_id;
  uint m_index;
  PointerRNA m_rna;
  Vector<SocketRef *> m_linked_sockets;
  Vector<SocketRef *> m_directly_linked_sockets;

  friend NodeTreeRef;

 public:
  ArrayRef<const SocketRef *> linked_sockets() const;
  ArrayRef<const SocketRef *> directly_linked_sockets() const;
  bool is_linked() const;

  const NodeRef &node() const;
  const NodeTreeRef &tree() const;

  uint id() const;
  uint index() const;

  bool is_input() const;
  bool is_output() const;

  const SocketRef &as_base() const;
  const InputSocketRef &as_input() const;
  const OutputSocketRef &as_output() const;

  PointerRNA *rna() const;

  StringRefNull idname() const;
  StringRefNull name() const;

  bNodeSocket *bsocket() const;
  bNode *bnode() const;
  bNodeTree *btree() const;
};

class InputSocketRef final : public SocketRef {
 public:
  ArrayRef<const OutputSocketRef *> linked_sockets() const;
  ArrayRef<const OutputSocketRef *> directly_linked_sockets() const;
};

class OutputSocketRef final : public SocketRef {
 public:
  ArrayRef<const InputSocketRef *> linked_sockets() const;
  ArrayRef<const InputSocketRef *> directly_linked_sockets() const;
};

class NodeRef : BLI::NonCopyable, BLI::NonMovable {
 private:
  NodeTreeRef *m_tree;
  bNode *m_bnode;
  PointerRNA m_rna;
  uint m_id;
  Vector<InputSocketRef *> m_inputs;
  Vector<OutputSocketRef *> m_outputs;

  friend NodeTreeRef;

 public:
  const NodeTreeRef &tree() const;

  ArrayRef<const InputSocketRef *> inputs() const;
  ArrayRef<const OutputSocketRef *> outputs() const;

  const InputSocketRef &input(uint index) const;
  const OutputSocketRef &output(uint index) const;

  bNode *bnode() const;
  bNodeTree *btree() const;

  PointerRNA *rna() const;
  StringRefNull idname() const;
  StringRefNull name() const;

  uint id() const;
};

class NodeTreeRef : BLI::NonCopyable, BLI::NonMovable {
 private:
  LinearAllocator<> m_allocator;
  bNodeTree *m_btree;
  Vector<NodeRef *> m_nodes_by_id;
  Vector<SocketRef *> m_sockets_by_id;
  Vector<InputSocketRef *> m_input_sockets;
  Vector<OutputSocketRef *> m_output_sockets;
  StringMap<Vector<NodeRef *>> m_nodes_by_idname;

 public:
  NodeTreeRef(bNodeTree *btree);
  ~NodeTreeRef();

  ArrayRef<const NodeRef *> nodes() const;
  ArrayRef<const NodeRef *> nodes_with_idname(StringRef idname) const;

  ArrayRef<const SocketRef *> sockets() const;
  ArrayRef<const InputSocketRef *> input_sockets() const;
  ArrayRef<const OutputSocketRef *> output_sockets() const;

  bNodeTree *btree() const;
};

/* --------------------------------------------------------------------
 * SocketRef inline methods.
 */

inline ArrayRef<const SocketRef *> SocketRef::linked_sockets() const
{
  return m_linked_sockets.as_ref();
}

inline ArrayRef<const SocketRef *> SocketRef::directly_linked_sockets() const
{
  return m_directly_linked_sockets.as_ref();
}

inline bool SocketRef::is_linked() const
{
  return m_linked_sockets.size() > 0;
}

inline const NodeRef &SocketRef::node() const
{
  return *m_node;
}

inline const NodeTreeRef &SocketRef::tree() const
{
  return m_node->tree();
}

inline uint SocketRef::id() const
{
  return m_id;
}

inline uint SocketRef::index() const
{
  return m_index;
}

inline bool SocketRef::is_input() const
{
  return m_is_input;
}

inline bool SocketRef::is_output() const
{
  return !m_is_input;
}

inline const SocketRef &SocketRef::as_base() const
{
  return *this;
}

inline const InputSocketRef &SocketRef::as_input() const
{
  BLI_assert(this->is_input());
  return *(const InputSocketRef *)this;
}

inline const OutputSocketRef &SocketRef::as_output() const
{
  BLI_assert(this->is_output());
  return *(const OutputSocketRef *)this;
}

inline PointerRNA *SocketRef::rna() const
{
  return const_cast<PointerRNA *>(&m_rna);
}

inline StringRefNull SocketRef::idname() const
{
  return m_bsocket->idname;
}

inline StringRefNull SocketRef::name() const
{
  return m_bsocket->name;
}

inline bNodeSocket *SocketRef::bsocket() const
{
  return m_bsocket;
}

inline bNode *SocketRef::bnode() const
{
  return m_node->bnode();
}

inline bNodeTree *SocketRef::btree() const
{
  return m_node->btree();
}

/* --------------------------------------------------------------------
 * InputSocketRef inline methods.
 */

inline ArrayRef<const OutputSocketRef *> InputSocketRef::linked_sockets() const
{
  return m_linked_sockets.as_ref().cast<const OutputSocketRef *>();
}

inline ArrayRef<const OutputSocketRef *> InputSocketRef::directly_linked_sockets() const
{
  return m_directly_linked_sockets.as_ref().cast<const OutputSocketRef *>();
}

/* --------------------------------------------------------------------
 * OutputSocketRef inline methods.
 */

inline ArrayRef<const InputSocketRef *> OutputSocketRef::linked_sockets() const
{
  return m_linked_sockets.as_ref().cast<const InputSocketRef *>();
}

inline ArrayRef<const InputSocketRef *> OutputSocketRef::directly_linked_sockets() const
{
  return m_directly_linked_sockets.as_ref().cast<const InputSocketRef *>();
}

/* --------------------------------------------------------------------
 * NodeRef inline methods.
 */

inline const NodeTreeRef &NodeRef::tree() const
{
  return *m_tree;
}

inline ArrayRef<const InputSocketRef *> NodeRef::inputs() const
{
  return m_inputs.as_ref();
}

inline ArrayRef<const OutputSocketRef *> NodeRef::outputs() const
{
  return m_outputs.as_ref();
}

inline const InputSocketRef &NodeRef::input(uint index) const
{
  return *m_inputs[index];
}

inline const OutputSocketRef &NodeRef::output(uint index) const
{
  return *m_outputs[index];
}

inline bNode *NodeRef::bnode() const
{
  return m_bnode;
}

inline bNodeTree *NodeRef::btree() const
{
  return m_tree->btree();
}

inline PointerRNA *NodeRef::rna() const
{
  return const_cast<PointerRNA *>(&m_rna);
}

inline StringRefNull NodeRef::idname() const
{
  return m_bnode->idname;
}

inline StringRefNull NodeRef::name() const
{
  return m_bnode->name;
}

inline uint NodeRef::id() const
{
  return m_id;
}

/* --------------------------------------------------------------------
 * NodeRef inline methods.
 */

inline ArrayRef<const NodeRef *> NodeTreeRef::nodes() const
{
  return m_nodes_by_id.as_ref();
}

inline ArrayRef<const NodeRef *> NodeTreeRef::nodes_with_idname(StringRef idname) const
{
  const Vector<NodeRef *> *nodes = m_nodes_by_idname.lookup_ptr(idname);
  if (nodes == nullptr) {
    return {};
  }
  else {
    return nodes->as_ref();
  }
}

inline ArrayRef<const SocketRef *> NodeTreeRef::sockets() const
{
  return m_sockets_by_id.as_ref();
}

inline ArrayRef<const InputSocketRef *> NodeTreeRef::input_sockets() const
{
  return m_input_sockets.as_ref();
}

inline ArrayRef<const OutputSocketRef *> NodeTreeRef::output_sockets() const
{
  return m_output_sockets.as_ref();
}

inline bNodeTree *NodeTreeRef::btree() const
{
  return m_btree;
}

}  // namespace BKE

#endif /* __BKE_NODE_TREE_REF_HH__ */
