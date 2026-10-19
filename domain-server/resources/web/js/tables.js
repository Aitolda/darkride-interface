$(document).ready(function(){
  // setup the underscore templates
  var nodeTemplate = _.template($('#nodes-template').html());
  var queuedTemplate = _.template($('#queued-template').html());
  var servicesTemplate = _.template($('#services-template').html());
  var peopleTemplate = _.template($('#people-template').html());

  // the servers a world runs, in the order they're shown, with what each looks after
  var SERVICES = [
    { type: "entity-server", name: "World objects", about: "Keeps everything in your world and saves it." },
    { type: "avatar-mixer", name: "Avatars", about: "Shares how everyone looks and moves." },
    { type: "audio-mixer", name: "Voice and sound", about: "Mixes voices and sounds so everyone hears what's near them." },
    { type: "asset-server", name: "File storage", about: "Holds the models, images and sounds uploaded to your world." },
    { type: "messages-mixer", name: "Messages", about: "Passes messages between visitors and scripts." },
    { type: "entity-script-server", name: "Server scripts", about: "Runs the scripts attached to objects in your world." },
    { type: "physics-server", name: "Physics", about: "Moves vehicles and other shared physics." }
  ];

  // a length of time in words: 45 seconds, 12 minutes, 3 hours, 2 days
  function durationText(seconds) {
    function plural(n, unit) {
      return n + " " + unit + (n === 1 ? "" : "s");
    }
    seconds = Math.floor(seconds);
    if (seconds < 60) {
      return plural(seconds, "second");
    }
    var minutes = Math.floor(seconds / 60);
    if (minutes < 60) {
      return plural(minutes, "minute");
    }
    var hours = Math.floor(minutes / 60);
    if (hours < 24) {
      return plural(hours, "hour") + (minutes % 60 ? ", " + plural(minutes % 60, "minute") : "");
    }
    var days = Math.floor(hours / 24);
    return plural(days, "day") + (hours % 24 ? ", " + plural(hours % 24, "hour") : "");
  }

  function showOverview(nodes, queued) {
    var queuedTypes = _.map(_.values(queued), function(assignment) { return assignment.type; });
    var services = [];
    _.each(SERVICES, function(service) {
      var node = _.find(nodes, function(node) { return node.type === service.type; });
      // a world that doesn't run a server (none connected or waiting) leaves it out
      if (!node && queuedTypes.indexOf(service.type) === -1) {
        return;
      }
      services.push(_.extend({}, service, {
        node: node,
        uptime: node ? durationText(parseFloat(node.uptime)) : ""
      }));
    });
    var running = _.filter(services, function(service) { return service.node; }).length;

    var people = _.map(_.filter(nodes, function(node) { return node.type === "agent"; }), function(node) {
      return _.extend({}, node, { uptimeText: durationText(parseFloat(node.uptime)) });
    });

    $('#dr-service-list').html(servicesTemplate({ services: services }));
    $('#dr-people-list').html(peopleTemplate({ people: people }));

    $('#dr-services-running').text(running + " of " + services.length);
    $('#dr-services-hint').text(services.length === 0 ? "Waiting for the servers to start" :
      running === services.length ? "Everything is running" : "Some are still starting");
    $('#dr-people-count').text(people.length);

    var longest = _.max(_.map(_.filter(nodes, function(node) { return node.type !== "agent"; }), function(node) {
      return parseFloat(node.uptime);
    }));
    $('#dr-uptime').text(isFinite(longest) ? durationText(longest) : "–");
  }

  var lastNodes = null;
  var lastQueued = null;
  function updateOverview() {
    if (lastNodes !== null && lastQueued !== null) {
      showOverview(lastNodes, lastQueued);
    }
  }

  // setup a function to grab the assignments
  function getNodesAndAssignments() {
    $.getJSON("nodes.json", function(json){

      json.nodes.sort(function(a, b){
          if (a.type === b.type) {
            if (a.uptime < b.uptime) {
              return 1;
            } else if (a.uptime > b.uptime) {
              return -1;
            } else {
              return 0;
            }
          }

          if (a.type === "agent" && b.type !== "agent") {
            return 1;
          } else if (b.type === "agent" && a.type !== "agent") {
            return -1;
          }

          if (a.type > b.type) {
            return 1;
          }

          if (a.type < b.type) {
            return -1;
          }
      });

      $('#nodes-table tbody').html(nodeTemplate(json));
      lastNodes = json.nodes;
      updateOverview();
    }).fail(function(jqXHR, textStatus, errorThrown) {
      // we assume a 401 means the DS has restarted
      // and no longer has our OAuth produced uuid
      // so just reload and re-auth
      if (jqXHR.status == 401) {
        location.reload();
      }
    });

    $.getJSON("assignments.json", function(json){
      $('#assignments-table tbody').html(queuedTemplate(json));
      lastQueued = json.queued;
      updateOverview();
    }).fail(function(jqXHR, textStatus, errorThrown) {
      // we assume a 401 means the DS has restarted
      // and no longer has our OAuth produced uuid
      // so just reload and re-auth
      if (jqXHR.status == 401) {
        location.reload();
      }
    });
  }

  // do the first GET on page load
  getNodesAndAssignments();
  // grab the new assignments JSON every two seconds
  var getNodesAndAssignmentsInterval = setInterval(getNodesAndAssignments, 2000);

  function deleteNode(uuid) {
    $.ajax({
        url: "/nodes/" + uuid,
        type: 'DELETE',
        success: function(result) {
          console.log("Succesful request to delete node.");
          getNodesAndAssignments();
        }
    });
  }

  // a server that's disconnected starts again by itself
  $(document.body).on('click', '.dr-restart-node', function(){
    var uuid = $(this).data('uuid');
    var name = $(this).data('name');
    swal({
      title: "Restart " + name + "?",
      text: "It stops and starts again by itself. Visitors may notice a short pause.",
      type: "warning",
      showCancelButton: true,
      confirmButtonText: "Restart",
      closeOnConfirm: true
    }, function() {
      deleteNode(uuid);
    });
  });

  // hook the node delete to the X button
  $(document.body).on('click', '.dr-kill-node', function(){
    deleteNode($(this).data('uuid'));
  });

  $('#dr-show-details').on('click', function() {
    var $details = $('#dr-details');
    $details.toggle();
    $(this).text($details.is(':visible') ? "Hide the details" : "Show the details");
  });

  $(document.body).on('click', '#kill-all-btn', function() {
    swal({
      title: "Restart all servers?",
      text: "Everyone in your world is disconnected for a moment while the servers start again.",
      type: "warning",
      showCancelButton: true,
      confirmButtonText: "Restart all",
      closeOnConfirm: true
    }, function() {
      $.ajax({
        url: "/nodes/",
        type: 'DELETE',
        success: function(result) {
          console.log("Successful request to delete all nodes.");
          getNodesAndAssignments();
        }
      });
    });
  });
});
