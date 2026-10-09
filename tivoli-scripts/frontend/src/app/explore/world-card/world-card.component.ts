import { Component, Input, OnInit } from "@angular/core";
import { MatSnackBar } from "@angular/material/snack-bar";
import { ScriptService } from "../../script.service";
import {
	ExploreService,
	InstanceVisibility,
	World,
} from "../explore.service";

@Component({
	selector: "app-world-card",
	templateUrl: "./world-card.component.html",
	styleUrls: ["./world-card.component.scss"],
})
export class WorldCardComponent implements OnInit {
	@Input() world: World;

	starting = false;

	constructor(
		public readonly exploreService: ExploreService,
		public readonly scriptService: ScriptService,
		private readonly snackBar: MatSnackBar,
	) {}

	displayPlural(n: number, singular: string, plural?: string) {
		return (
			n +
			" " +
			(n == 1 ? singular : plural != null ? plural : singular + "s")
		);
	}

	ngOnInit() {}

	get users(): string {
		if (this.world.online && !this.world.instances?.instanceOnly) {
			return this.displayPlural(this.world.numUsers, "person", "people");
		} else if (this.world.instances) {
			const people = this.world.instances.list.reduce(
				(n, i) => n + i.players,
				0,
			);
			return this.displayPlural(people, "person", "people");
		} else {
			return "Offline";
		}
	}

	get instanceSummary(): string {
		const list = this.world.instances?.list ?? [];
		return list.length == 0
			? "No instances running"
			: this.displayPlural(list.length, "instance");
	}

	// the thumbnail joins the always-on domain when there is one, otherwise an instance
	onThumbnail() {
		const instances = this.world.instances;
		if (instances && (instances.instanceOnly || !this.world.online)) {
			if (instances.list.length > 0) {
				this.onJoinInstance();
			} else if (instances.canStart) {
				this.onNewInstance("public");
			}
		} else {
			this.onJoinWorld();
		}
	}

	// the instance with the most people in it (the list comes sorted)
	onJoinInstance() {
		const instance = this.world.instances.list[0];
		if (instance) this.exploreService.joinInstance(instance);
	}

	onNewInstance(visibility: InstanceVisibility) {
		if (this.starting) return;
		this.starting = true;
		this.exploreService.startInstance(this.world.id, visibility).subscribe(
			instance => {
				this.starting = false;
				this.exploreService.joinInstance(instance);
			},
			err => {
				this.starting = false;
				this.snackBar.open(
					"Could not start an instance: " +
						(err?.error?.message ?? err?.message ?? "unknown error"),
					"OK",
					{ duration: 8000 },
				);
			},
		);
	}

	onJoinWorld() {
		this.scriptService.emitEvent(
			"explore",
			"joinDomain",
			this.world.id + this.world.path,
		);
		this.scriptService.emitEvent("explore", "close");
	}

	onLikeWorld() {
		this.exploreService
			.likeWorld(this.world.id, !this.world.liked)
			.subscribe(() => {
				this.world.liked = !this.world.liked;
				if (this.world.liked) {
					this.world.likes++;
				} else {
					this.world.likes--;
				}
			});
	}
}
