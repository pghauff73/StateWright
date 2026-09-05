#!/usr/bin/env bash
# Offline URL construction; native validation supplies registry defaults.
set -euo pipefail
statewright=${1:?usage: bash Tools/generate_saa_watchlist_100.sh BINARY NEW_MANIFEST_PATH}
output=${2:?provide a new manifest path}
registry=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/resources/watchlists/internet/source-groups-v1.json
if [[ -e "$output" ]]; then
  printf 'refusing to overwrite manifest: %s\n' "$output" >&2
  exit 1
fi
request=$(jq -nc --arg output "$output" --slurpfile registry "$registry" '
  ["algorithm", "program synthesis", "symbolic reasoning", "abstract interpretation",
   "constraint solving", "affine transformation", "numerical integration", "interpolation",
   "root finding", "linear algebra", "exact arithmetic", "interval arithmetic",
   "graph algorithms", "shortest path", "dynamic programming", "string matching",
   "sorting algorithm", "optimization algorithm", "automatic differentiation",
   "probabilistic inference"] as $topics |
  [1321,1950,1951,1952,2104,3174,3629,3986,4648,5054,5869,6234,6979,7296,7515,7516,7517,7518,7539,7693] as $rfcs |
  ["rdf-canon","json-ld11","json-ld11-api","json-ld11-framing","xpath-31",
   "xpath-functions-31","xquery-31","xslt-30","xml-c14n11","xml-exc-c14n",
   "xmlenc-core1","xmldsig-core1","rdf11-concepts","rdf11-mt","sparql11-query",
   "sparql11-update","sparql11-entailment","turtle","shacl","prov-constraints"] as $w3c |
  {action:"create",template:"exact",watchlist_version:"saa-all-lanes-100-v2",
   description:"100 unique SAA URLs: Crossref 20, Europe PMC 20, RFC 20, W3C 20, pinned file-reviewed Fungrim 10 and Boost.Math 10; replaces disabled NIST targets.",
   output_path:$output,
   watches: (
     [$topics[] | {name:("crossref-" + gsub(" ";"-")),source_group:"crossref",subject:.,
       canonical_url:("https://api.crossref.org/works?query.title=" + @uri +
         "&filter=type:journal-article&sort=updated&order=desc&rows=10&select=DOI,title,URL,license,updated")}] +
     [$topics[] | {name:("europe-pmc-" + gsub(" ";"-")),source_group:"europe-pmc",subject:.,
       canonical_url:("https://www.ebi.ac.uk/europepmc/webservices/rest/search?query=" +
         (("OPEN_ACCESS:y AND TITLE:\"" + . + "\"")|@uri) + "&format=json&resultType=core&pageSize=10")}] +
     [$registry[0].source_groups[] | select(.source_group == "fungrim" or .source_group == "boost-math") |
       .source_group as $group | .reviewed_sources | keys[] |
       {name:($group + "-" + (split("/")[-1] | sub("\\.(py|qbk)$";"") | gsub("_";"-"))),
        source_group:$group,canonical_url:.}] +
     [$rfcs[] | {name:("rfc-" + tostring),source_group:"ietf-rfc-editor",
       canonical_url:("https://www.rfc-editor.org/rfc/rfc" + tostring + ".html")}] +
     [$w3c[] | {name:("w3c-" + .),source_group:"w3c",canonical_url:("https://www.w3.org/TR/" + . + "/")}]
     | map(. + {enabled:true}))}')
"$statewright" internet-watchlist "$request"
jq -e '(.watches|length)==100 and ([.watches[].canonical_url]|unique|length)==100 and
  ([.watches|group_by(.source_group)[]|length] == [10,20,20,10,20,20]) and
  all(.watches[]; .source_group != "nist-dlmf")' "$output" >/dev/null
