create virtual table webpkg_fts using fts5(id,
                                           pkgstem,
                                           comment,
                                           descr_contents,
                                           maintainer);

insert into webpkg_fts
select pathid,
       pkgstem,
       comment,
       descr_contents,
       maintainer
  from portsq;
