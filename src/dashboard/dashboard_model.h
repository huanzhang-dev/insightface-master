#pragma once
#include "auth_pipeline.h"
#include <stdexcept>
#include <cstdio>

namespace fa {
// Shared by the window and the end-to-end dashboard check. Only its worker uses it.
class DashboardModel {
public:
    std::unique_ptr<AuthPipeline> pipeline;
    Image photo;
    void require(bool ok, const std::string& message) const {
        if (!ok) throw std::runtime_error(message);
    }
    void open(const std::string& profile_path, const std::string& database) {
        require(!database.empty(), "Choose a database file first.");
        ModelProfile profile;
        require(load_profile(profile_path, ".", profile), "Cannot load the model profile.");
        auto next = std::make_unique<AuthPipeline>();
        require(next->init(profile, database), "Cannot open models or database. Check the profile, feature dimensions, file permissions and database lock.");
        pipeline = std::move(next);
    }
    AuthPipeline& ready() {
        require(pipeline != nullptr, "Open a database first.");
        return *pipeline;
    }
    void single_face() {
        require(!photo.empty(), "Load a photo or capture from the camera first.");
        std::vector<Face> faces;
        require(ready().detector().detect(photo, faces) && faces.size() == 1,
                "Use a photo with exactly one visible face.");
    }
    void enroll(const std::string& id, const std::string& name, const std::string& card) {
        require(!id.empty() && !name.empty(), "Person ID and name are required.");
        single_face();
        require(ready().enroll_image(photo, id, name, card),
                "Registration failed. Check duplicate ID/card, face landmarks, and database write permissions.");
    }
    std::string authenticate(const std::string& card) {
        single_face();
        auto& p = ready();
        Feature feature;
        require(p.extract_from_image(photo, feature), "Cannot extract this face. Try a clearer photo.");
        int index = -1;
        float score = kNoScore;
        if (card.empty()) {
            std::vector<MatchResult> matches;
            p.store().matcher().match({feature}, matches);
            index = matches[0].index;
            score = matches[0].score;
        } else {
            CardMatch match;
            require(p.store().match_cards({feature}, {card}, match), "Card is not registered. Authentication rejected.");
            index = match.cards[0].record;
            score = match.cards[0].best_score;
        }
        char numbers[120];
        std::snprintf(numbers, sizeof(numbers), " (score %.3f; threshold %.3f)", score, p.match_threshold());
        if (index < 0 || score < p.match_threshold()) return std::string("NOT RECOGNIZED") + numbers;
        const auto& person = p.store().records()[index];
        return "RECOGNIZED: " + person.name + " [" + person.id + "]" + numbers;
    }
};
}
