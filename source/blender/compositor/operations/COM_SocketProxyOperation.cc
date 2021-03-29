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
 *
 * Copyright 2011, Blender Foundation.
 */

#include "COM_SocketProxyOperation.h"

namespace blender::compositor {

SocketProxyOperation::SocketProxyOperation(DataType type, bool use_conversion)
    : m_use_conversion(use_conversion)
{
  this->addInputSocket(type);
  this->addOutputSocket(type);
}

std::unique_ptr<MetaData> SocketProxyOperation::getMetaData()
{
  return this->getInputSocket(0)->getReader()->getMetaData();
}

}  // namespace blender::compositor
