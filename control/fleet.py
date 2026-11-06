#!/usr/bin/env python3
"""Sequential, acknowledgement-based rollout to up to 16 loopback agents/tunnels."""
import argparse
import hashlib
import http.client
import json
import pathlib
import urllib.parse


def load_plan(path):
    path = pathlib.Path(path).resolve()
    if path.stat().st_size > 65536:
        raise ValueError("plan exceeds 64 KiB")
    plan = json.loads(path.read_text())
    if not isinstance(plan, list) or not 1 <= len(plan) <= 16:
        raise ValueError("plan must contain 1..16 nodes")
    result, names, urls = [], set(), set()
    for node in plan:
        if not isinstance(node, dict) or set(node) != {"name", "url", "token_file", "config", "expected_generation"}:
            raise ValueError("invalid node fields")
        url = urllib.parse.urlsplit(node["url"])
        if url.scheme != "http" or url.hostname != "127.0.0.1" or not url.port or url.path not in ("", "/") or url.query or url.fragment or url.username:
            raise ValueError("use an explicit 127.0.0.1 HTTP port, optionally forwarded through SSH")
        name = node["name"]
        if not isinstance(name, str) or not name or len(name) > 64 or name in names or url.port in urls:
            raise ValueError("duplicate/invalid name or endpoint")
        if type(node["expected_generation"]) is not int or node["expected_generation"] < 1:
            raise ValueError("positive expected generation required")
        with (path.parent / node["config"]).open("rb") as source:
            config = source.read(65537)
        if not 1 <= len(config) <= 65536:
            raise ValueError("configuration exceeds bounds")
        with (path.parent / node["token_file"]).open("r") as source:
            token = source.read(130).strip()
        if not 16 <= len(token) <= 128 or not all(c.isalnum() or c in "_-" for c in token):
            raise ValueError("invalid bearer token")
        names.add(name); urls.add(url.port)
        result.append({**node, "port": url.port, "body": config.decode("utf-8"), "token": token,
                       "sha256": hashlib.sha256(config).hexdigest()})
    return result


def request(node, method, path, body=None):
    connection = http.client.HTTPConnection("127.0.0.1", node["port"], timeout=8)
    try:
        connection.request(method, path, body=body, headers={"Authorization": "Bearer " + node["token"],
                                                            "Content-Type": "application/json"})
        response = connection.getresponse()
        raw = response.read(65537)
        if len(raw) > 65536:
            raise RuntimeError("oversize agent response")
        result = json.loads(raw)
        if response.status != 200:
            raise RuntimeError("agent rejected request: " + str(result.get("error", response.status)))
        return result
    finally:
        connection.close()


def rollout(nodes):
    results = []
    # Preflight the whole fleet before the first publication. This is not consensus:
    # another publisher can race, and each agent still compares expected generation.
    for node in nodes:
        status = request(node, "GET", "/v1/status")
        if status.get("generation") != node["expected_generation"] or not status.get("running") or status.get("uncertain"):
            raise RuntimeError("preflight failed for " + node["name"])
    for node in nodes:
        try:
            body = json.dumps({"expected_generation": node["expected_generation"], "config": node["body"]}, ensure_ascii=False).encode()
            if len(body) > 80000:
                raise ValueError("encoded configuration exceeds agent request bound")
            status = request(node, "POST", "/v1/config", body)
            if not status.get("running") or status.get("uncertain") or type(status.get("generation")) is not int or status["generation"] <= node["expected_generation"]:
                raise RuntimeError("invalid publication acknowledgement")
            results.append({"name": node["name"], "published": True, "generation": status["generation"], "sha256": node["sha256"]})
        except (OSError, ValueError, RuntimeError, http.client.HTTPException) as error:
            results.append({"name": node["name"], "published": False, "error": str(error)})
            break
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["plan", "rollout"])
    parser.add_argument("manifest")
    args = parser.parse_args()
    nodes = load_plan(args.manifest)
    if args.action == "plan":
        print(json.dumps([{"name": n["name"], "url": n["url"], "expected_generation": n["expected_generation"], "sha256": n["sha256"]} for n in nodes], indent=2))
    else:
        results = rollout(nodes)
        print(json.dumps(results, indent=2))
        if len(results) != len(nodes) or not all(r["published"] for r in results):
            raise SystemExit(1)


if __name__ == "__main__":
    main()
