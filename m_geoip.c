/*
 * GeoIP lookup module for Solanum IRCd
 * Fetches location information from a MaxMind DB
 */

#include "stdinc.h"
#include "modules.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "hook.h"
#include "s_conf.h"
#include <maxminddb.h>

#define GEOIP_DB_PATH "/home/debian/IRC/ircd/solanum/run/geodb/GeoLite2-City.mmdb"

static void mo_geoip(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static int do_geoip_lookup(struct Client *source_p, struct Client *target_p);

struct Message geoip_msgtab = {
    "GEOIP", 0, 0, 0, 0,
    { mg_ignore, mg_ignore, mg_ignore, mg_ignore, { mo_geoip, 2 }, { mo_geoip, 2 } }
};

mapi_clist_av1 geoip_clist[] = { &geoip_msgtab, NULL };

static const char geoip_desc[] = "Provides GeoIP lookup for users";

DECLARE_MODULE_AV2(geoip, NULL, NULL, geoip_clist, NULL, NULL, NULL, NULL, geoip_desc);

static void
mo_geoip(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
    if (parc < 2)  // Ensure correct parameter count
    {
        sendto_one(source_p, ":%s NOTICE %s :Usage: GEOIP <nick>", me.name, source_p->name);
        return;
    }

    struct Client *target_p = find_named_person(parv[1]);
    if (target_p == NULL)
    {
        sendto_one(source_p, ":%s NOTICE %s :No such nick %s", me.name, source_p->name, parv[1]);
        return;
    }

    do_geoip_lookup(source_p, target_p);
}

static int do_geoip_lookup(struct Client *source_p, struct Client *target_p)
{
    if (!target_p || !target_p->host)
    {
        sendto_one(source_p, ":%s NOTICE %s :GeoIP lookup failed - user has no valid IP/hostname.", me.name, source_p->name);
        return 0;
    }

    MMDB_s mmdb;
    int status = MMDB_open(GEOIP_DB_PATH, MMDB_MODE_MMAP, &mmdb);
    if (status != MMDB_SUCCESS)
    {
        sendto_one(source_p, ":%s NOTICE %s :GeoIP database is missing or corrupted!", me.name, source_p->name);
        logerror("geoip: Failed to open %s: %s", GEOIP_DB_PATH, MMDB_strerror(status));
        return 0;
    }

    int gai_error, mmdb_error;
    MMDB_lookup_result_s result = MMDB_lookup_string(&mmdb, target_p->host, &gai_error, &mmdb_error);
    if (mmdb_error != MMDB_SUCCESS)
    {
        sendto_one(source_p, ":%s NOTICE %s :No GeoIP data found for %s", me.name, source_p->name, target_p->name);
        loginfo("geoip: No GeoIP data found for %s", target_p->host);
        MMDB_close(&mmdb);
        return 0;
    }

    MMDB_entry_data_s country_data, city_data, continent_data;
    const char *country_name = "Unknown";
    const char *city_name = "Unknown";
    const char *continent_name = "Unknown";

    // Always fetch in English (ensuring no foreign characters)
    if (MMDB_get_value(&result.entry, &country_data, "country", "names", "en", NULL) == MMDB_SUCCESS && country_data.has_data)
        country_name = country_data.utf8_string;

    if (MMDB_get_value(&result.entry, &city_data, "city", "names", "en", NULL) == MMDB_SUCCESS && city_data.has_data)
        city_name = city_data.utf8_string;

    if (MMDB_get_value(&result.entry, &continent_data, "continent", "names", "en", NULL) == MMDB_SUCCESS && continent_data.has_data)
        continent_name = continent_data.utf8_string;

    sendto_one(source_p, ":%s NOTICE %s :User %s is from %s, %s (%s)", me.name, source_p->name, target_p->name, city_name, country_name, continent_name);
    MMDB_close(&mmdb);
    return 1;
}

