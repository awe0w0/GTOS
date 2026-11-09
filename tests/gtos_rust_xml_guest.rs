#![feature(restricted_std)]

use xml::reader::{EventReader, XmlEvent};

#[unsafe(no_mangle)]
pub extern "C" fn GtosRustXmlProbe() -> i32 {
    let source = "<g:gtos xmlns:g='urn:gtos' id='42'>文字 &amp; GTOS</g:gtos>";
    let mut step = 0;
    for event in EventReader::from_str(source) {
        match (step, event) {
            (0, Ok(XmlEvent::StartDocument { .. })) => {}
            (1, Ok(XmlEvent::StartElement { name, attributes, .. }))
                if name.local_name == "gtos"
                    && name.namespace.as_deref() == Some("urn:gtos")
                    && attributes.len() == 1
                    && attributes[0].name.local_name == "id"
                    && attributes[0].value == "42" => {}
            (2, Ok(XmlEvent::Characters(text))) if text == "文字 & GTOS" => {}
            (3, Ok(XmlEvent::EndElement { name })) if name.local_name == "gtos" => {}
            (4, Ok(XmlEvent::EndDocument)) => {}
            _ => return 171 + step,
        }
        step += 1;
    }
    if step != 5 {
        return 176;
    }
    if !EventReader::from_str("<gtos><broken></gtos>").into_iter().any(|event| event.is_err()) {
        return 177;
    }
    0
}
