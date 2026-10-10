import { SignalManager } from "./lib/signal-manager";

// The metaverse's users list for the domain we are in, fetched once and shared by the nametags, the
// chat and the Tab overview: session id ("{...}") → the account behind that avatar. In round 1 this is
// where an AI resident's mark, its own word and its host come from (AI-FIRST-CLIENT.md §10.4). An
// avatar is matched to its account by the session id that account reported to the metaverse, never
// by the display name it shows.

export interface CachedUser {
	username: string;
	ai: boolean;
	aiWord: string;
	host: string;
	thumbnail: string;
	connection: string;
}

export type CachedUsers = { [sessionId: string]: CachedUser };

// the line under a resident's name: "✦ language model · hosted by caitlyn"
export function markLine(user: { aiWord?: string; host?: string }) {
	return (
		"✦ " +
		(user.aiWord || "AI resident") +
		(user.host ? " · hosted by " + user.host : "")
	);
}

const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

class UsersCache {
	private bySession: CachedUsers = {};
	private listeners: ((users: CachedUsers) => any)[] = [];
	private signals = new SignalManager();

	private started = false;
	private interval: object = null;
	private fetchTimeout: object = null;
	private lastFetchAt = 0;

	private readonly minFetchIntervalMs = 5 * 1000;
	private readonly pollIntervalMs = 20 * 1000;

	get(sessionId: string): CachedUser | null {
		if (sessionId == null) return null;
		this.start();
		return this.bySession[sessionId] || null;
	}

	all(): CachedUsers {
		this.start();
		return this.bySession;
	}

	onUpdate(listener: (users: CachedUsers) => any) {
		this.start();
		this.listeners.push(listener);
		return listener;
	}

	offUpdate(listener: (users: CachedUsers) => any) {
		this.listeners = this.listeners.filter(l => l !== listener);
	}

	// a fetch soon, at most one every few seconds (arrivals and departures call this)
	refreshSoon() {
		this.start();
		const due = this.lastFetchAt + this.minFetchIntervalMs - Date.now();
		if (due <= 0) {
			this.fetch();
		} else if (this.fetchTimeout == null) {
			this.fetchTimeout = Script.setTimeout(() => {
				this.fetchTimeout = null;
				this.fetch();
			}, due);
		}
	}

	private start() {
		if (this.started) return;
		this.started = true;

		this.interval = Script.setInterval(() => this.fetch(), this.pollIntervalMs);
		this.signals.connect(AvatarList.avatarAddedEvent, () => this.refreshSoon());
		this.signals.connect(AvatarList.avatarRemovedEvent, () => this.refreshSoon());
		this.signals.connect(AddressManager.hostChanged, () => {
			this.bySession = {};
			this.refreshSoon();
		});

		this.fetch();
	}

	private fetch() {
		const domainID = AddressManager.domainID;
		if (domainID == null || domainID == "" || domainID == NULL_UUID) return;

		this.lastFetchAt = Date.now();

		const req = new XMLHttpRequest();
		req.onreadystatechange = () => {
			if (req.readyState != 4 || req.status != 200) return;

			let json: any;
			try {
				json = JSON.parse(req.responseText);
			} catch (err) {
				return;
			}
			if (json.status != "success" || json.data == null || json.data.users == null)
				return;

			const users: CachedUsers = {};
			for (const u of json.data.users) {
				if (u.location == null || !u.location.node_id) continue;
				users["{" + u.location.node_id + "}"] = {
					username: u.username,
					ai: u.ai === true,
					aiWord: u.ai_word || "",
					host: u.host || "",
					thumbnail: u.images ? u.images.thumbnail : "",
					connection: u.connection,
				};
			}
			this.bySession = users;

			for (const listener of this.listeners) {
				try {
					listener(users);
				} catch (err) {
					console.error(err);
				}
			}
		};
		req.open(
			"GET",
			AccountServices.metaverseServerURL +
				"/api/v1/users?per_page=1000&status=" +
				domainID.slice(1, -1) +
				"&a=" +
				Date.now(),
			true,
		);
		req.send();
	}

	cleanup() {
		if (this.interval) Script.clearInterval(this.interval);
		if (this.fetchTimeout) Script.clearTimeout(this.fetchTimeout);
		this.interval = null;
		this.fetchTimeout = null;
		this.signals.cleanup();
		this.listeners = [];
		this.bySession = {};
		this.started = false;
	}
}

export const usersCache = new UsersCache();
