// ----------------------------------------------------------------------------
// -- Pico.css modal handling

const isOpenClass       = 'modal-is-open';
const openingClass      = 'modal-is-opening';
const closingClass      = 'modal-is-closing';
const animationDuration = 400; // ms
let   visibleModal      = null;

const getScrollbarWidth = () => {
    // Creating invisible container
    const outer = document.createElement('div');
    outer.style.visibility = 'hidden';
    outer.style.overflow = 'scroll'; // forcing scrollbar to appear
    outer.style.msOverflowStyle = 'scrollbar'; // needed for WinJS apps
    document.body.appendChild(outer);
    
    // Creating inner element and placing it in the container
    const inner = document.createElement('div');
    outer.appendChild(inner);
    
    // Calculating difference between container's full width and the child width
    const scrollbarWidth = (outer.offsetWidth - inner.offsetWidth);
    
    // Removing temporary elements from the DOM
    outer.parentNode.removeChild(outer);
    
    return scrollbarWidth;
    }
    
    // Is scrollbar visible
const isScrollbarVisible = () => {
    return document.body.scrollHeight > screen.height;
}

let OpenModal = (modal) => {
    if (isScrollbarVisible()) {
        document.documentElement.style.setProperty('--scrollbar-width', `${getScrollbarWidth()}px`);
    }
    document.documentElement.classList.add(isOpenClass, openingClass);
    setTimeout(() => {
        visibleModal = modal;
        document.documentElement.classList.remove(openingClass);
    }, animationDuration);
    modal.setAttribute('open', true);
};

let CloseModal = (modal) => {
    visibleModal = null;
    document.documentElement.classList.add(closingClass);
    setTimeout(() => {
        document.documentElement.classList.remove(closingClass, isOpenClass);
        document.documentElement.style.removeProperty('--scrollbar-width');
        modal.removeAttribute('open');
    }, animationDuration);
};

let onMenuButton = (e) => {
    e.preventDefault();
    const modal = document.getElementById(e.currentTarget.getAttribute('data-target'));
    if (typeof(modal) != 'undefined' && modal != null)
    {
        OpenModal(modal);
        LE_StartUpdateStats();
    }
};

let onCloseButton = (e) => {
    e.preventDefault();
    const modal = document.getElementById(e.currentTarget.getAttribute('data-target'));
    if (typeof(modal) != 'undefined' && modal != null)
    {
        LE_StopUpdateStats();
        CloseModal(modal);
    }
};

let onSaveAndCloseButton = (e) => {
    $.get("/save_settings", {
        waitDuration:   $("#hungriness").val(),
        cycleOvershoot: $("#cycle-overshoot").val(),
        emptyOvershoot: $("#empty-overshoot").val()
    }, () => { console.log("Settings sent OK") });
    onCloseButton(e);
};

let Pico_SetupModalDynamics = () => {    
    document.addEventListener('click', event => {
        if (visibleModal != null) {
            const modalContent  = visibleModal.querySelector('article');
            const isClickInside = modalContent.contains(event.target);
            if (!isClickInside)
            {
                LE_StopUpdateStats();
                CloseModal(visibleModal);
            }
        }
    });
}

// ----------------------------------------------------------------------------
// -- LitterEater code

let LE_AllStates = [
    "Initializing",
    "Idle",
    "Cat inside",
    "Waiting for cycle",
    "Cycling (step 1)",
    "Cycling (step 2)",
    "Cycling (step 2)",
    "Cycling (step 3)",
    "Cycling (resuming)",
    "Cycling (cat sensor triggered)",
    "Emptying (step 1)",
    "Emptying (waiting on user)",
    "Emptying (step 2)",
    "Emptying (resuming)",
    "Emptying (cat sensor triggered)",
    "Disabled"
];

let LE_Settings_FirstUpdate   = true;
let LE_Settings_KeepGoing     = false;

let LE_StopUpdateStats = () => {
    document.getElementById("current-status").setAttribute("aria-busy", "true");
    LE_Settings_FirstUpdate = true;
    LE_Settings_KeepGoing = false;
}

let LE_StartUpdateStats = () => {
    LE_StopUpdateStats();
    LE_Settings_KeepGoing = true;
    LE_UpdateStats();
};

let LE_ProcessUpdate = (data) => {
    if (document.getElementById("current-status").hasAttribute("aria-busy"))
        document.getElementById("current-status").removeAttribute("aria-busy");

    $("#current-status").text(LE_AllStates[data.state]);

    if (LE_Settings_FirstUpdate)
    {
        $("#hungriness").val(data.waitduration);
        $("#cycle-overshoot").val(data.cycleOvershoot);
        $("#empty-overshoot").val(data.emptyOvershoot);

        $("#hungriness-output").text(data.waitduration);
        $("#cycle-overshoot-output").text(data.cycleOvershoot);
        $("#empty-overshoot-output").text(data.emptyOvershoot);

        LE_Settings_FirstUpdate = false;
    }

    if (LE_Settings_KeepGoing)
        setTimeout(LE_UpdateStats, 5000);
};

let LE_ProcessUpdateError = (data) => {
    if (document.getElementById("current-status").hasAttribute("aria-busy"))
        document.getElementById("current-status").removeAttribute("aria-busy");

    $("#current-status").text("Offline");

    if (LE_Settings_KeepGoing)
        setTimeout(LE_UpdateStats, 30000);
};

let LE_UpdateStats = () => {
    $.ajax({
        url: "/stats",
        success: LE_ProcessUpdate,
        error: LE_ProcessUpdateError,
        timeout: 2200 //in milliseconds
    });
};

let LE_onToggleEnabled = () => {
    if (document.getElementById("mainswitch").checked)
        $.getJSON("/enable", (data) => {});
    else
        $.getJSON("/disable", (data) => {});
};

// ----------------------------------------------------------------------------
// -- Entrypoint

$(() => {
    Pico_SetupModalDynamics();

    $.getJSON("/isenabled", (data) => {
        if (document.getElementById("mainswitch-label").hasAttribute("aria-busy"))
            document.getElementById("mainswitch-label").removeAttribute("aria-busy");
        
        $("#mainswitch").prop('checked', data.result);
    });
});