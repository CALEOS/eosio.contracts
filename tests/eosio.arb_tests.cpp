#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/wast_to_wasm.hpp>

#include <Runtime/Runtime.h>
#include <iomanip>

#include <fc/variant_object.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"
#include "eosio.arb_tester.hpp"

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(eosio_arb_tests)

BOOST_FIXTURE_TEST_CASE( full_election, eosio_arb_tester ) try {
   auto one_day = 86400;

   // are start_election and election_duration supposed to be time frames or time stamps ? 
   // from code it feels like a time frame ?

   uint32_t start_election = 300, election_duration = 300;

   // setup and check configuration
   setconfig (
      20, 
      start_election, 
      election_duration,            
      (one_day * 10), 
      vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)})
   );
   produce_blocks(1);

   auto config = get_config();
   BOOST_REQUIRE_EQUAL(false, config.is_null());
   REQUIRE_MATCHING_OBJECT(
      config, 
      mvo()
         ("publisher", eosio::chain::name("eosio.arb"))
         ("max_elected_arbs", uint16_t(20))
         ("election_duration", election_duration)
         ("start_election", start_election)
         ("fee_structure", vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)}))
         ("arbitrator_term_length", uint32_t(one_day * 10))
         ("last_time_edited", now())
         ("ballot_id", 0)
         ("auto_start_election", false)
   );

   // initialize election => start new election with 20 seats 
   init_election();
   uint32_t expected_begin_time = uint32_t(now() + start_election);
   uint32_t expected_end_time = expected_begin_time + election_duration;
   produce_blocks(1);

   auto cbid = config["ballot_id"].as_uint64();   

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   BOOST_REQUIRE_EQUAL(expected_begin_time, leaderboard["begin_time"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(expected_end_time, leaderboard["end_time"].as<uint32_t>());
   
   // exceptions : "ballot doesn't exist" and "leaderboard doesn't exist" mean the following checks should fail
      // if they didn't yet got the error, check the code - can't check here
   // verify correct assignments of available primary keys
   BOOST_REQUIRE_EQUAL(bid, lid);
   BOOST_REQUIRE_EQUAL(cbid, lid);

   // cannot init another election while one is in progress
   BOOST_REQUIRE_EXCEPTION( 
      init_election(),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Election is on auto start mode." )
   );
   
   // choose 3 candidates
   name candidate1 = test_voters[0];
   name candidate2 = test_voters[1];
   name candidate3 = test_voters[2];
   name dropout_candidate = test_voters[3];
   name noncandidate = test_voters[4];

   symbol vote_symbol = symbol(4, "VOTE");
   register_voters(test_voters, 5, 30, vote_symbol);

   // verify they aren't registered yet
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate1.value).is_null());
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate2.value).is_null());
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate3.value).is_null());

   for(int i = 0; i <= 3; i++){
      // register 
      regarb( test_voters[i], std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );
      produce_blocks(1);

      // check integrity
      auto c = get_candidate(test_voters[i].value);
      BOOST_REQUIRE_EQUAL( c["cand_name"].as<name>(), test_voters[i] );
      BOOST_REQUIRE_EQUAL( c["credential_link"],  std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );
   }

   // candidates cannot register multiple times
   BOOST_REQUIRE_EXCEPTION( 
      regarb(dropout_candidate, std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition2/")), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Candidate is already an applicant" )
   );
   
   // check unregister candidate1 + dropout_candidate
   unregarb(dropout_candidate.value);
   unregarb(candidate1.value);
   produce_blocks(1);

   // verify unregister went through
   BOOST_REQUIRE_EQUAL(true, get_candidate(dropout_candidate.value).is_null());

   // non-candidates cannot unregister
   BOOST_REQUIRE_EXCEPTION( 
      unregarb(dropout_candidate.value),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Candidate isn't an applicant" )
   );

   // register candidate1 back before start of election
   regarb( candidate1, std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );
   produce_blocks(1);

   // check integrity
   auto c = get_candidate(candidate1.value);
   BOOST_REQUIRE_EQUAL( c["cand_name"].as<name>(), candidate1 );
   BOOST_REQUIRE_EQUAL( c["credential_link"],  std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );

   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);
   
   for(int i = 5; i < 30; i++) {
      mirrorcast(test_voters[i].value, symbol(4, "TLOS"));
  
      uint16_t vote_direction_0 = 0;      
      uint16_t vote_direction_1 = ( i % 3 == 0 ) ? uint16_t(2) : uint16_t(1);
      
      castvote(test_voters[i].value, config["ballot_id"].as_uint64(), vote_direction_0);
      castvote(test_voters[i].value, config["ballot_id"].as_uint64(), vote_direction_1);

      produce_blocks(1);
   }

   // re-registerd dropout during election => he will be part of next election
   regarb(dropout_candidate, std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/"));
   produce_blocks(1);  

   // election can't be ended until it finishes
   BOOST_REQUIRE_EXCEPTION( 
      endelection(noncandidate),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "election isn't ended." )
   );

   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);

   // noncandidate isn't part of the election 
   BOOST_REQUIRE_EXCEPTION( 
      endelection(noncandidate),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Candidate isn't an applicant." )
   );
   
   // end the election = > candidates 1-3 in, dropout for next election
   expected_begin_time = uint32_t(now() + start_election);
   expected_end_time = expected_begin_time + election_duration;
   endelection(candidate1);
   produce_blocks(1);

   // next election check
   config = get_config();
   auto next_cbid = config["ballot_id"].as_uint64();   

   // new election has new ballot different from previous one
   BOOST_REQUIRE_NE(next_cbid, cbid);

   ballot = get_ballot(next_cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   // check election times
   BOOST_REQUIRE_EQUAL(expected_begin_time, leaderboard["begin_time"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(expected_end_time, leaderboard["end_time"].as<uint32_t>());
   
   BOOST_REQUIRE_EQUAL(bid, lid);
   BOOST_REQUIRE_EQUAL(next_cbid, lid);

   // check integrity
   c = get_candidate(dropout_candidate.value);
   BOOST_REQUIRE_EQUAL( c["cand_name"].as<name>(), dropout_candidate );
   BOOST_REQUIRE_EQUAL( c["credential_link"],  std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );

   BOOST_REQUIRE_EXCEPTION( 
       init_election(),
       eosio_assert_message_exception, 
       eosio_assert_message_is( "Election is on auto start mode." )
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( election_automode_off, eosio_arb_tester ) try {
   auto one_day = 86400;

   // are start_election and election_duration supposed to be time frames or time stamps ? 
   // from code it feels like a time frame ?

   uint32_t start_election = 300, election_duration = 300;

   // setup and check configuration
   setconfig (
      2, 
      start_election, 
      election_duration,            
      (one_day * 40), 
      vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)})
   );
   produce_blocks(1);

   auto config = get_config();
   BOOST_REQUIRE_EQUAL(false, config.is_null());
   REQUIRE_MATCHING_OBJECT(
      config, 
      mvo()
         ("publisher", eosio::chain::name("eosio.arb"))
         ("max_elected_arbs", uint16_t(2))
         ("election_duration", election_duration)
         ("start_election", start_election)
         ("fee_structure", vector<int64_t>({int64_t(1), int64_t(2), int64_t(3), int64_t(4)}))
         ("arbitrator_term_length", uint32_t(one_day * 40))
         ("last_time_edited", now())
         ("ballot_id", 0)
         ("auto_start_election", false)
   );

   init_election();
   uint32_t expected_begin_time = uint32_t(now() + start_election);
   uint32_t expected_end_time = expected_begin_time + election_duration;
   produce_blocks(1);

   auto cbid = config["ballot_id"].as_uint64();   

   auto ballot = get_ballot(cbid);
   auto bid = ballot["reference_id"].as_uint64();

   auto leaderboard = get_leaderboard(bid);
   auto lid = leaderboard["board_id"].as_uint64();

   BOOST_REQUIRE_EQUAL(expected_begin_time, leaderboard["begin_time"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL(expected_end_time, leaderboard["end_time"].as<uint32_t>());
   
   // exceptions : "ballot doesn't exist" and "leaderboard doesn't exist" mean the following checks should fail
   // if they didn't yet got the error, check the code - can't check here
   // verify correct assignments of available primary keys
   BOOST_REQUIRE_EQUAL(bid, lid);
   BOOST_REQUIRE_EQUAL(cbid, lid);

   // cannot init another election while one is in progress
   BOOST_REQUIRE_EXCEPTION( 
      init_election(),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Election is on auto start mode." )
   );
   
   // choose 3 candidates
   name candidate1 = test_voters[0];
   name candidate2 = test_voters[1];
   name candidate3 = test_voters[2];
   name candidate4 = test_voters[3];
   name dropout_candidate = test_voters[4];
   name noncandidate = test_voters[5];

   symbol vote_symbol = symbol(4, "VOTE");
   register_voters(test_voters, 6, 30, vote_symbol);

   // verify they aren't registered yet
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate1.value).is_null());
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate2.value).is_null());
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate3.value).is_null());
   BOOST_REQUIRE_EQUAL(true, get_candidate(candidate4.value).is_null());

   for(int i = 0; i <= 4; i++){
      // register 
      regarb( test_voters[i], std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );
      produce_blocks(1);

      // check integrity
      auto c = get_candidate(test_voters[i].value);
      BOOST_REQUIRE_EQUAL( c["cand_name"].as<name>(), test_voters[i] );
      BOOST_REQUIRE_EQUAL( c["credential_link"],  std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );
   }

   // candidates cannot register multiple times
   BOOST_REQUIRE_EXCEPTION( 
      regarb(dropout_candidate, std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition2/")), 
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Candidate is already an applicant" )
   );
   
   // check unregister candidate1 + dropout_candidate
   unregarb(dropout_candidate.value);
   produce_blocks(1);

   // verify unregister went through
   BOOST_REQUIRE_EQUAL(true, get_candidate(dropout_candidate.value).is_null());

   // non-candidates cannot unregister
   BOOST_REQUIRE_EXCEPTION( 
      unregarb(dropout_candidate.value),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "Candidate isn't an applicant" )
   );

   produce_blocks(1);

   // check integrity
   auto c = get_candidate(candidate1.value);
   BOOST_REQUIRE_EQUAL( c["cand_name"].as<name>(), candidate1 );
   BOOST_REQUIRE_EQUAL( c["credential_link"],  std::string("/ipfs/53CharacterLongHashToSatisfyIPFSHashCondition1/") );

   // start the election period
   produce_block(fc::seconds(start_election));
   produce_blocks(1);
   
   //Check tie conflicts
   for(int i = 6; i < 30; i++) {
      mirrorcast(test_voters[i].value, symbol(4, "TLOS"));
  
      uint16_t vote_direction_0 = 0;      
      uint16_t vote_direction_1 = ( i % 21 == 0 ) ? uint16_t(2) : uint16_t(1);
      uint16_t vote_direction_2 = ( i % 10 == 0 ) ? uint16_t(2) : uint16_t(3);
      castvote(test_voters[i].value, config["ballot_id"].as_uint64(), vote_direction_0);
      castvote(test_voters[i].value, config["ballot_id"].as_uint64(), vote_direction_1);
      castvote(test_voters[i].value, config["ballot_id"].as_uint64(), vote_direction_2);
      
      produce_blocks(1);
   }

   // election can't be ended until it finishes
   BOOST_REQUIRE_EXCEPTION ( 
      endelection(noncandidate),
      eosio_assert_message_exception, 
      eosio_assert_message_is( "election isn't ended." )
   );

   // election period is over
   produce_block(fc::seconds(election_duration));
   produce_blocks(1);


   // end the election = > candidates 1-3 in, dropout for next election
   expected_begin_time = uint32_t(now() + start_election);
   expected_end_time = expected_begin_time + election_duration;
   endelection(candidate1);
   produce_blocks(1);

   // next election check
   config = get_config();
   auto next_cbid = config["ballot_id"].as_uint64();   

   // new election has new ballot different from previous one
   BOOST_REQUIRE_EQUAL(next_cbid, cbid);

   ballot = get_ballot(next_cbid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   //New election started   
   init_election();   
   produce_blocks(1);

   auto new_bid = config["ballot_id"].as_uint64();
   
   BOOST_REQUIRE_EQUAL(next_cbid, new_bid);

   ballot = get_ballot(new_bid);
   bid = ballot["reference_id"].as_uint64();

   leaderboard = get_leaderboard(bid);
   lid = leaderboard["board_id"].as_uint64();

   BOOST_REQUIRE_EQUAL(bid, lid);
   BOOST_REQUIRE_EQUAL(new_bid, lid);         

     
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()