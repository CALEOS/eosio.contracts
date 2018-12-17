#include <fox.protocol.hpp>
//#include <eosiolib/print.hpp>

fox::fox(name self, name code, datastream<const char*> ds) : contract(self, code, ds) {
}

fox::~fox() {
}

void fox::regdomain(name publisher, symbol native_symbol) {
    require_auth(publisher);

    domains_table domains(get_self(), get_self().value);
    auto d_itr = domains.find(native_symbol.code().raw());
    eosio_assert(d_itr == domains.end(), "Domain with that symbol already exists");

    domains.emplace(publisher, [&](auto& l) { //NOTE: publisher pays for own registration
        l.native_symbol = native_symbol;
        l.publisher = publisher;
        l.reg_time = now();
    });
}

void fox::unregdomain(name publisher, symbol native_symbol) {
    require_auth(publisher);

    domains_table domains(get_self(), get_self().value);
    auto d_itr = domains.find(native_symbol.code().raw());
    eosio_assert(d_itr != domains.end(), "Domain with that symbol doesn't exist");

    domains.erase(d_itr);
}

void fox::blacklist(name host, symbol sym_to_blacklist) {
    require_auth(host);
    eosio_assert(host == get_self(), "Only host account can blacklist");

    domains_table domains(get_self(), get_self().value);
    auto d_itr = domains.find(sym_to_blacklist.code().raw());
    eosio_assert(d_itr != domains.end(), "Domain with that symbol doesn't exist");

    domains.modify(d_itr, same_payer, [&](auto& l) {
        l.is_blacklisted = true;
    });
}

// void fox::setconfig(name publisher, config new_config) {
//     require_auth(publisher);
//     //TODO: update config with new_config values
// }

EOSIO_DISPATCH(fox, (regdomain)(unregdomain)(blacklist))