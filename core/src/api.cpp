#include "expansion/api.hpp"

#include "expansion/state_codec.hpp"

namespace expansion::api {
namespace {

// One place where an internal exception becomes typed error information.
// `fallback` classifies anything the core did not classify itself.
template <typename T, typename Fn>
Outcome<T> guard(ErrorCode fallback, Fn&& fn) {
  try {
    return Outcome<T>::success(fn());
  } catch (const SimError& e) {
    return Outcome<T>::failure(e.code() == ErrorCode::Internal ? fallback : e.code(), e.what());
  } catch (const std::exception& e) {
    return Outcome<T>::failure(ErrorCode::Internal, std::string("unexpected failure: ") + e.what());
  } catch (...) {
    return Outcome<T>::failure(ErrorCode::Internal, "unexpected failure of an unknown kind");
  }
}

}  // namespace

Outcome<CatalogPtr> load_catalog(const ContentSource& source) {
  return guard<CatalogPtr>(ErrorCode::InvalidContent, [&] {
    return CatalogPtr(std::make_shared<const Catalog>(Catalog::load(source)));
  });
}

Outcome<SessionPtr> create_session(CatalogPtr catalog, const std::string& scenario_id,
                                   const std::string& faction_id, std::uint64_t seed) {
  if (!catalog) return Outcome<SessionPtr>::failure(ErrorCode::InvalidArgument, "no catalog was supplied");
  return guard<SessionPtr>(ErrorCode::NotFound, [&] {
    return SessionPtr(Session::create(*catalog, scenario_id, faction_id, seed).release());
  });
}

CommandResult apply_command(Session& session, const Command& command, std::optional<Revision> expected_revision) {
  return session.apply_command(command, expected_revision);
}

Outcome<DayResult> step_day(Session& session) {
  return guard<DayResult>(ErrorCode::InvariantViolated, [&] { return session.step_day(); });
}

Outcome<ForecastResult> forecast(const Session& session, const std::vector<Command>& candidate_commands, int days) {
  return guard<ForecastResult>(ErrorCode::InvalidArgument,
                               [&] { return session.forecast(candidate_commands, days); });
}

Outcome<std::string> encode_save(const Session& session, const std::string& build_id) {
  return guard<std::string>(ErrorCode::Internal, [&] {
    json::Value payload = encode_state(session.state(), session.catalog());
    // The envelope lives in the persistence module; the core exposes only the
    // canonical payload, which a host may wrap however its platform requires.
    (void)build_id;
    return json::serialize_canonical(payload);
  });
}

Outcome<SessionPtr> decode_save(CatalogPtr catalog, const std::string& bytes) {
  if (!catalog) return Outcome<SessionPtr>::failure(ErrorCode::InvalidArgument, "no catalog was supplied");
  return guard<SessionPtr>(ErrorCode::CorruptSave, [&] {
    SessionState state = decode_state(json::parse(bytes), *catalog);
    return SessionPtr(Session::from_state(*catalog, std::move(state)).release());
  });
}

Outcome<std::string> canonical_hash(const Session& session) {
  return guard<std::string>(ErrorCode::Internal, [&] { return session.canonical_hash(); });
}

std::vector<ScenarioSummary> scenarios(const Catalog& catalog) {
  std::vector<ScenarioSummary> out;
  for (const auto& s : catalog.scenarios()) {
    out.push_back({s.id, s.display_key, s.completion.evaluation_day});
  }
  return out;
}

std::vector<FactionSummary> playable_factions(const Catalog& catalog) {
  std::vector<FactionSummary> out;
  for (const FactionDef* f : catalog.playable_factions()) {
    out.push_back({f->id, f->display_key});
  }
  return out;
}

}  // namespace expansion::api
