/*
 * Copyright (c) 2012, Igalia, S.L.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

if (!window.epiphany) {
    window.epiphany = {};
}

window.epiphany.forms = window.epiphany.forms || {

    find_username_and_password_elements: function (form) {
	if (!form.elements || form.elements.length == 0)
	    return {username: null, password: null};

	username_node = null;
	password_node = null;
	for (i in form.elements) {
	    element = form.elements[i];

	    if (element['type']) {
		if (element['type'] == 'text' ||
		    element['type'] == 'email') {
		    if (username_node) {
			/* We found more than one inputs of type text; we won't be
			 * saving here */
			username_node = null;
			break;
		    }
		    username_node = element;
		} else if (element['type'] == 'password') {
		    if (password_node) {
			password_node = null;
			break;
		    }
		    password_node = element;
		}
	    }
	}	
	return {username:username_node, password:password_node};
    },

    prefill_form: function (username_node, password_node, login_data) {
	username_name = username_node['name'];
	password_name = password_node['name'];

	for (i in login_data) {
	    if (username_name == login_data[i].user_name &&
		password_name == login_data[i].password_name) {
		username_node['value'] = login_data[i].user_value;
		password_node['value'] = login_data[i].password_value;
	    }
	}
    },

    fill_forms: function (login_data) {
	if (login_data.length == 0)
	    return;
	
	for (i in document.forms) {
	    form = document.forms[i];

	    nodes = this.find_username_and_password_elements (form);
	    
	    if (nodes.username && nodes.password) {
		this.prefill_form (nodes.username, nodes.password, login_data)
	    }
	}
    }
}
