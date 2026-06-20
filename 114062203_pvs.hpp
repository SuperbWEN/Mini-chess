#pragma once
#include <vector>
#include "114062203_state.hpp"
#include "search_types.hpp"
#include "game_history.hpp"
#include "114062203_submission.hpp"

class PVS{
public:
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
