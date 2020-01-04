const LibDS = require('LibDS/LibDS');
const Protocol = require('LibDS/protocols/Protocol2019');

var protocol = new Protocol({
    'team_number':3487,

    get_joystick_data(robot_info) {
        const userAction = async () => {
            const response = await("http://qira.local/qira_robot_data", {
                method: 'POST',
                body: robot_info,
                headers: {
                    'Content-Type': 'application/json',
                }
            });
            return await response.json();
        } 
    },
    on_started() {
        const userAction = async() => {
            const response = await("http://qira.local/robot_started", {
                method: 'POST',
                body: {},
                headers: {
                    'Content-Type': 'application/json',
                }
            });
        }
    },
    on_stopped() {
        const userAction = async() => {
            const response = await("http://qira.local/robot_stopped", {
                method: 'POST',
                body: {},
                headers: {
                    'Content-Type': 'application/json',
                }
            });
        }
    },

    

});

