/*****************************************************************************\
 *  Copyright (c) 2019 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef QUEUE_POLICY_HYBRID_IMPL_HPP
#define QUEUE_POLICY_HYBRID_IMPL_HPP

#include "qmanager/policies/queue_policy_hybrid.hpp"

namespace Flux {
namespace queue_manager {
namespace detail {

template<class reapi_type>
queue_policy_hybrid_t<reapi_type>::~queue_policy_hybrid_t ()
{

}

template<class reapi_type>
queue_policy_hybrid_t<reapi_type>::queue_policy_hybrid_t ()
{
    queue_policy_bf_base_t<reapi_type>::m_reservation_depth =
        HYBRID_RESERVATION_DEPTH;
}

template<class reapi_type>
int queue_policy_hybrid_t<reapi_type>::apply_params ()
{
    int rc = -1;
    try {
        std::unordered_map<std::string, std::string>::const_iterator i;
        if ((i = queue_policy_base_impl_t::m_params.find ("queue-depth"))
             != queue_policy_base_impl_t::m_params.end ()) {
            unsigned int depth = std::stoi (i->second);
            if (depth < MAX_QUEUE_DEPTH)
                queue_policy_base_impl_t::m_queue_depth = depth;
        }
        if ((i = queue_policy_base_impl_t::m_params.find ("reservation-depth"))
             != queue_policy_base_impl_t::m_params.end ()) {
            unsigned int depth = std::stoi (i->second);
            if (depth < MAX_RESERVATION_DEPTH)
                queue_policy_bf_base_t<reapi_type>::m_reservation_depth = depth;
        }
        rc = 0;
    } catch (const std::invalid_argument &e) {
        errno = EINVAL;
    } catch (const std::out_of_range &e) {
        errno = ERANGE;
    }
    return rc; 
}

} // namespace Flux::queue_manager::detail
} // namespace Flux::queue_manager
} // namespace Flux

#endif // QUEUE_POLICY_HYBRID_IMPL_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
