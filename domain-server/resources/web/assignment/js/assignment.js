$(document).ready(function(){

  // setup the editor
  var editor = ace.edit("editor");
  editor.setTheme("ace/theme/twilight")
  editor.getSession().setMode("ace/mode/javascript");
  editor.getSession().setTabSize(2);

  var messageTimer = null;
  function showMessage(html, isError) {
    clearTimeout(messageTimer);
    $('#run-message').html(html).toggleClass('run-ok', !isError).toggleClass('run-error', !!isError).fadeIn(150);
    messageTimer = setTimeout(function() {
      $('#run-message').fadeOut(300);
    }, isError ? 8000 : 6000);
  }

  $('#deploy-button').click(function(){
    script = editor.getValue();

    // setup our boundary - this is "highfidelity" in hex
    var boundary = "----68696768666964656c697479";
    var body = '--' + boundary + '\r\n'
      // parameter name is "file" and local filename is "temp.txt"
      + 'Content-Disposition:form-data; name="file"; '
      + 'filename="script.js"\r\n'
      // add the javascript mime-type
      + 'Content-type: application/javascript\r\n\r\n'
      // add the script
      + script + '\r\n'
      + '--' + boundary + '--\r\n';

    var headers = {};

    if ($('#instance-field input').val()) {
      headers['ASSIGNMENT-INSTANCES'] = $('#instance-field input').val();
    }

    if ($('#pool-field input').val()) {
      headers['ASSIGNMENT-POOL'] = $('#pool-field input').val();
    }

    var $button = $(this).prop('disabled', true);

    // post form to assignment in order to create an assignment
    $.ajax({
        contentType: "multipart/form-data; boundary=" + boundary,
        data: body,
        type: "POST",
        url: "/assignment",
        headers: headers,
        success: function (data, status) {
          var copies = parseInt(headers['ASSIGNMENT-INSTANCES'] || "1", 10);
          showMessage("<strong>Started.</strong> " + (copies > 1 ? copies + " copies start" : "It starts") +
            " as soon as a script runner is free. You'll see " + (copies > 1 ? "them" : "it") +
            " under People here now on the <a href='/'>Overview</a>.");
        },
        error: function () {
          showMessage("<strong>The script couldn't be sent.</strong> Check that the world server is running, and try again.", true);
        },
        complete: function () {
          $button.prop('disabled', false);
        }
    });
  });
});
