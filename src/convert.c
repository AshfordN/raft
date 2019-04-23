#include "convert.h"
#include "assert.h"
#include "configuration.h"
#include "election.h"
#include "log.h"
#include "progress.h"
#include "queue.h"

/* Convenience for setting a new state value and asserting that the transition
 * is valid. */
static void setState(struct raft *r, int state)
{
    /* Check that the transition is legal, see Figure 3.3. Note that with
     * respect to the paper we have an additional "unavailable" state, which is
     * the initial or final state. */
    assert((r->state == RAFT_UNAVAILABLE && state == RAFT_FOLLOWER) ||
           (r->state == RAFT_FOLLOWER && state == RAFT_CANDIDATE) ||
           (r->state == RAFT_CANDIDATE && state == RAFT_FOLLOWER) ||
           (r->state == RAFT_CANDIDATE && state == RAFT_LEADER) ||
           (r->state == RAFT_LEADER && state == RAFT_FOLLOWER) ||
           (r->state == RAFT_FOLLOWER && state == RAFT_UNAVAILABLE) ||
           (r->state == RAFT_CANDIDATE && state == RAFT_UNAVAILABLE) ||
           (r->state == RAFT_LEADER && state == RAFT_UNAVAILABLE));
    r->state = state;
}

/* Clear follower state. */
static void clearFollower(struct raft *r)
{
    r->follower_state.current_leader.id = 0;
    if (r->follower_state.current_leader.address != NULL) {
        raft_free(r->follower_state.current_leader.address);
    }
    r->follower_state.current_leader.address = NULL;
}

/* Clear candidate state. */
static void clearCandidate(struct raft *r)
{
    if (r->candidate_state.votes != NULL) {
        raft_free(r->candidate_state.votes);
        r->candidate_state.votes = NULL;
    }
}

/* Clear leader state. */
static void clearLeader(struct raft *r)
{
    if (r->leader_state.progress != NULL) {
        raft_free(r->leader_state.progress);
        r->leader_state.progress = NULL;
    }

    /* If a promotion request is in progress and we are waiting for the server
     * to be promoted to catch up with logs, then we need to abort the
     * promotion, because having lost leadership we're not in the position to
     * submit any raft entry.
     *
     * Note that if a promotion request is in progress but we're not waiting for
     * the server to be promoted to catch up with logs, then it means that the
     * server was up to date at some point and we submitted the configuration
     * change to turn it into a voting server. Even if we lost leadership, it
     * could be that the entry still gets committed, so we don't abort the
     * promotion just yet.
     *
     * TODO: create a request object for promotion requests.
     */

    /* Fail all outstanding apply requests */
    while (!QUEUE_IS_EMPTY(&r->leader_state.requests)) {
        struct raft_apply *req;
        queue *head;
        head = QUEUE_HEAD(&r->leader_state.requests);
        QUEUE_REMOVE(head);
        req = QUEUE_DATA(head, struct raft_apply, queue);
        if (req->cb != NULL) {
	  req->cb(req, RAFT_LEADERSHIPLOST, NULL);
        }
    }
}

/* Clear the current state */
static void clear(struct raft *r)
{
    assert(r->state == RAFT_UNAVAILABLE || r->state == RAFT_FOLLOWER ||
           r->state == RAFT_CANDIDATE || r->state == RAFT_LEADER);
    switch (r->state) {
        case RAFT_FOLLOWER:
            clearFollower(r);
            break;
        case RAFT_CANDIDATE:
            clearCandidate(r);
            break;
        case RAFT_LEADER:
            clearLeader(r);
            break;
    }
}

void convertToFollower(struct raft *r)
{
    clear(r);
    setState(r, RAFT_FOLLOWER);

    /* Reset election timer. */
    electionResetTimer(r);

    r->follower_state.current_leader.id = 0;
    r->follower_state.current_leader.address = NULL;
}

int convertToCandidate(struct raft *r)
{
    size_t n_voting = configurationNumVoting(&r->configuration);
    int rv;

    clear(r);
    setState(r, RAFT_CANDIDATE);

    /* Allocate the votes array. */
    r->candidate_state.votes = raft_malloc(n_voting * sizeof(bool));
    if (r->candidate_state.votes == NULL) {
        return RAFT_NOMEM;
    }

    /* Start a new election round */
    rv = electionStart(r);
    if (rv != 0) {
        r->state = RAFT_FOLLOWER;
        raft_free(r->candidate_state.votes);
        return rv;
    }
    return 0;
}

int convertToLeader(struct raft *r)
{
    int rv;

    clear(r);
    setState(r, RAFT_LEADER);

    /* Reset timers */
    r->election_timer_start = r->io->time(r->io);

    /* Reset apply requests queue */
    QUEUE_INIT(&r->leader_state.requests);

    /* Allocate and initialize the progress array. */
    rv = progressBuildArray(r);
    if (rv != 0) {
        return rv;
    }

    /* Reset promotion state. */
    r->leader_state.promotee_id = 0;
    r->leader_state.round_number = 0;
    r->leader_state.round_index = 0;
    r->leader_state.round_start = 0;

    return 0;
}

void convertToUnavailable(struct raft *r)
{
    clear(r);
    setState(r, RAFT_UNAVAILABLE);
}