let setupHandlers = function() {
    // Resize handler
    window.addEventListener('resize', onResizeCanvas, false);

    let canvas = document.getElementById("canvas");
    document.addEventListener('keydown', function(e){
        // Must either have ctrl down OR nothing selected to activate
        if (event.ctrlKey===false &&
            $('*:focus').length > 0) return;
        if (event.ctrlKey===true &&
            (event.which === 61 || event.which === 107 ||
                event.which === 173 || event.which === 109  || event.which === 187  ||
                event.which === 189 || event.which === 72 ||
                event.which === 79 || event.which === 80)) {
            event.preventDefault();
        }
        Volrend.on_key(e.which, e.ctrlKey, e.shiftKey, e.altKey);
    });
    canvas.addEventListener("mousedown", function(e){
        if (e.button == 0) {
            let oleft = $(canvas).offset().left;
            let otop = $(canvas).offset().top;
            Volrend.on_mousedown(e.clientX - oleft, e.clientY - otop);
        }
    });
    var pinch_zoom_dist = -1.;
    var touch_cen_x = -1.;
    var touch_cen_y = -1.;
    canvas.addEventListener("touchstart", function(e){
        if (e.touches.length > 0) {
            touch_cen_x = 0.; touch_cen_y = 0.;
            for (let i = 0; i < e.touches.length; i++) {
                touch_cen_x += e.touches[i].pageX;
                touch_cen_y += e.touches[i].pageY;
            }
            touch_cen_x /= e.touches.length;
            touch_cen_y /= e.touches.length;
            let oleft = $(canvas).offset().left;
            let otop = $(canvas).offset().top;
            touch_cen_x -= oleft;
            touch_cen_y -= otop;
            if (e.touches.length == 2) {
                let touch1 = e.touches[0],
                    touch2 = e.touches[1];
                let dist = Math.hypot(
                    touch1.pageX - touch2.pageX,
                    touch1.pageY - touch2.pageY);
                pinch_zoom_dist = dist;
            }
            Volrend.on_mousedown(e.touches[0].pageX - oleft,
                e.touches[0].pageY - otop);
            e.preventDefault();
        }
    });
    canvas.addEventListener("mousemove", function(e){
        if (e.button == 0) {
            let oleft = $(canvas).offset().left;
            let otop = $(canvas).offset().top;
            Volrend.on_mousemove(e.clientX - oleft, e.clientY - otop);
        }
    });
    canvas.addEventListener("touchmove", function(e){
        if (e.touches.length > 0) {
            if (e.touches.length == 2) {
                let touch1 = e.touches[0],
                    touch2 = e.touches[1];
                let dist = Math.hypot(
                    touch1.pageX - touch2.pageX,
                    touch1.pageY - touch2.pageY);
                if (pinch_zoom_dist > 0.001) {
                    let delta_y = pinch_zoom_dist - dist;
                    Volrend.on_mousewheel(delta_y < 0,
                        Math.max(Math.abs(delta_y) * 10., 1.0001),
                        touch_cen_x, touch_cen_y);
                }
                pinch_zoom_dist = dist;
            }
            let oleft = $(canvas).offset().left;
            let otop = $(canvas).offset().top;
            Volrend.on_mousemove(e.touches[0].pageX - oleft,
                e.touches[0].pageY - otop);
            e.preventDefault();
        }
    });
    canvas.addEventListener("mouseup", function(e){
        if (e.button == 0) {
            let oleft = $(canvas).offset().left;
            let otop = $(canvas).offset().top;
            Volrend.on_mouseup(e.clientX - oleft, e.clientY - otop);
        }
    });
    canvas.addEventListener("touchend", function(){
        Volrend.on_mouseup(0, 0);
        pinch_zoom_dist = -1.;
    });
    canvas.addEventListener("wheel", function(e){
        if(e.ctrlKey)
            event.preventDefault();//prevent zoom
        Volrend.on_mousewheel(e.deltaY < 0,
            15., e.offsetX, e.offsetY);
    });
};
let onInit = function() {
    setupHandlers()
    glfwPatch();
};
