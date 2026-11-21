#pragma once
// foobar profile-local Title Formatting templates only. Never approvals/writes.
#include "djmeta/structural_guard.h"
#include <algorithm>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {
struct NamedRouteProfile {
    std::string name, destination_root, titleformat_expression;
    bool operator==(const NamedRouteProfile&) const = default;
};
inline constexpr std::size_t max_named_route_profiles=24;
inline bool route_name_equal(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) {
        const auto fold=[](unsigned char c) {
            return c>='A' && c<='Z' ? static_cast<unsigned char>(c+32) : c;
        };
        if(fold(static_cast<unsigned char>(a[i]))!=
           fold(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
inline bool reserved_route_name(std::string_view name) noexcept {
    return route_name_equal(name,"Singles") ||
           route_name_equal(name,"Alben") ||
           route_name_equal(name,"Livesets");
}
inline bool route_text_valid(std::string_view value,std::size_t limit) {
    return !value.empty() && value.size()<=limit &&
        valid_utf8_metadata_text(value) &&
        std::none_of(value.begin(),value.end(),[](unsigned char c) {
            return c<0x20 || c==0x7f;
        });
}
inline bool named_route_valid(const NamedRouteProfile& p) {
    return route_text_valid(p.name,128) && !reserved_route_name(p.name) &&
           route_text_valid(p.destination_root,4096) &&
           route_text_valid(p.titleformat_expression,8192);
}
inline std::string route_hex(std::string_view raw) {
    constexpr char digits[]="0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size()*2);
    for(unsigned char c:raw) {
        out.push_back(digits[c>>4]);out.push_back(digits[c&15]);
    }
    return out;
}
inline std::optional<std::string> route_unhex(std::string_view hex) {
    if(hex.size()%2) return std::nullopt;
    const auto digit=[](char c)->int {
        if(c>='0'&&c<='9') return c-'0';
        if(c>='A'&&c<='F') return c-'A'+10;
        return -1;
    };
    std::string out;
    out.reserve(hex.size()/2);
    for(std::size_t i=0;i<hex.size();i+=2) {
        const int hi=digit(hex[i]),lo=digit(hex[i+1]);
        if(hi<0||lo<0) return std::nullopt;
        out.push_back(static_cast<char>((hi<<4)|lo));
    }
    return out;
}
inline std::string serialize_named_routes(const std::vector<NamedRouteProfile>& profiles) {
    if(profiles.size()>max_named_route_profiles)
        throw std::invalid_argument("Too many named routes");
    std::string out="v1|"+std::to_string(profiles.size());
    for(std::size_t i=0;i<profiles.size();++i) {
        const auto& p=profiles[i];
        if(!named_route_valid(p))
            throw std::invalid_argument("Invalid named route");
        for(std::size_t j=0;j<i;++j)
            if(route_name_equal(profiles[j].name,p.name))
                throw std::invalid_argument("Duplicate route name");
        out+="|"+route_hex(p.name)+"|"+route_hex(p.destination_root)+
             "|"+route_hex(p.titleformat_expression);
        if(out.size()>131072)
            throw std::invalid_argument("Named routes exceed storage bound");
    }
    return out;
}
// Fail closed on unknown schema, duplicates, truncation or malformed Unicode.
inline std::optional<std::vector<NamedRouteProfile>>
parse_named_routes(std::string_view raw) {
    if(raw.size()<4||raw.size()>131072||!raw.starts_with("v1|"))
        return std::nullopt;
    std::size_t pos=3;
    const auto next=[&](std::string_view& field)->bool {
        if(pos>raw.size()) return false;
        const auto end=raw.find('|',pos);
        field=raw.substr(pos,end==std::string_view::npos?end:end-pos);
        pos=end==std::string_view::npos?raw.size()+1:end+1;
        return true;
    };
    std::string_view n;
    if(!next(n)||n.empty()||(n.size()>1&&n.front()=='0'))
        return std::nullopt;
    std::size_t count=0;
    const auto [end,ec]=std::from_chars(n.data(),n.data()+n.size(),count);
    if(ec!=std::errc{}||end!=n.data()+n.size()||count>max_named_route_profiles)
        return std::nullopt;
    std::vector<NamedRouteProfile> profiles;
    profiles.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        std::string_view a,b,c;
        if(!next(a)||!next(b)||!next(c)) return std::nullopt;
        auto name=route_unhex(a),root=route_unhex(b),expr=route_unhex(c);
        if(!name||!root||!expr) return std::nullopt;
        NamedRouteProfile p{std::move(*name),std::move(*root),std::move(*expr)};
        if(!named_route_valid(p) ||
           std::any_of(profiles.begin(),profiles.end(),[&](const auto& old) {
               return route_name_equal(old.name,p.name);
           })) return std::nullopt;
        profiles.push_back(std::move(p));
    }
    if(pos!=raw.size()+1) return std::nullopt;
    return profiles;
}
inline bool upsert_named_route(std::vector<NamedRouteProfile>& profiles,
                               const NamedRouteProfile& p,bool overwrite) {
    if(!named_route_valid(p)) throw std::invalid_argument("Invalid named route");
    auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto& old) {
        return route_name_equal(old.name,p.name);
    });
    if(it!=profiles.end()) {
        if(*it==p) return false;
        if(!overwrite) throw std::invalid_argument("Route name exists");
        *it=p;
    } else {
        if(profiles.size()>=max_named_route_profiles)
            throw std::invalid_argument("Named route limit reached");
        profiles.push_back(p);
    }
    (void)serialize_named_routes(profiles);
    return true;
}
inline bool erase_named_route(std::vector<NamedRouteProfile>& profiles,
                              std::string_view name) {
    auto it=std::find_if(profiles.begin(),profiles.end(),[&](const auto& p) {
        return route_name_equal(p.name,name);
    });
    if(it==profiles.end()) return false;
    profiles.erase(it);
    return true;
}

inline constexpr std::string_view named_route_transfer_prefix =
    "MMS-NAMED-ROUTES/1\n";

// Explicit clipboard transfer of templates only. No approval, media metadata,
// provider credentials or user files are included. Destination roots are
// user-supplied configuration and must not be logged or sent to public CI.
inline std::string export_named_routes_transfer(
    const std::vector<NamedRouteProfile>& profiles) {
    return std::string(named_route_transfer_prefix) +
           serialize_named_routes(profiles);
}
inline std::optional<std::vector<NamedRouteProfile>>
parse_named_routes_transfer(std::string_view transfer) {
    if(!transfer.starts_with(named_route_transfer_prefix) ||
       transfer.size()>131072+named_route_transfer_prefix.size())
        return std::nullopt;
    return parse_named_routes(transfer.substr(named_route_transfer_prefix.size()));
}

struct NamedRouteImportPlan {
    std::vector<NamedRouteProfile> merged;
    std::size_t added=0, replaced=0, unchanged=0;
    bool changed() const noexcept { return added!=0 || replaced!=0; }
};

// Atomic in-memory all-or-nothing merge; caller owns the one confirmation
// and the final profile-local cfg_string update. Never stages a file route.
inline NamedRouteImportPlan plan_named_routes_import(
    const std::vector<NamedRouteProfile>& existing,
    const std::vector<NamedRouteProfile>& incoming) {
    (void)serialize_named_routes(existing);
    (void)serialize_named_routes(incoming);
    NamedRouteImportPlan plan;
    plan.merged=existing;
    for(const auto& candidate:incoming) {
        auto it=std::find_if(plan.merged.begin(),plan.merged.end(),
            [&](const auto& old) {
                return route_name_equal(old.name,candidate.name);
            });
        if(it==plan.merged.end()) {
            if(plan.merged.size()>=max_named_route_profiles)
                throw std::invalid_argument("Import exceeds named route limit");
            plan.merged.push_back(candidate);
            ++plan.added;
        } else if(*it==candidate) {
            ++plan.unchanged;
        } else {
            *it=candidate;
            ++plan.replaced;
        }
    }
    (void)serialize_named_routes(plan.merged);
    return plan;
}
} // namespace djmeta
